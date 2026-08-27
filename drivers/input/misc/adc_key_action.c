#include <linux/module.h>
#include <linux/input.h>
#include <linux/reboot.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#define LONG_PRESS_MS  3000
#define TARGET_KEY     KEY_RESTART

static ktime_t press_time;
static bool key_down;

/* ---- workqueue 上下文执行重启/关机（可睡眠） ---- */
enum key_action { ACTION_NONE, ACTION_REBOOT, ACTION_POWEROFF };

static struct key_action_work {
	struct work_struct work;
	enum key_action action;
} g_action_work;

static void do_key_action(struct work_struct *work)
{
	struct key_action_work *aw = container_of(work, struct key_action_work, work);

	switch (aw->action) {
	case ACTION_REBOOT:
		pr_info("adc-key: executing reboot\n");
		kernel_restart(NULL);
		break;
	case ACTION_POWEROFF:
		pr_info("adc-key: executing poweroff\n");
		kernel_power_off();
		break;
	default:
		break;
	}
}

/* ---- filter：仅记录时间 + 调度 work，绝不调用任何可能睡眠的函数 ---- */
static bool adc_key_filter(struct input_handle *h,
			   unsigned int type, unsigned int code, int value)
{
	if (type != EV_KEY || code != TARGET_KEY)
		return false;

	if (value == 1) {				/* 按下 */
		press_time = ktime_get();
		key_down = true;
	} else if (value == 0 && key_down) {		/* 释放 */
		s64 dur = ktime_ms_delta(ktime_get(), press_time);
		key_down = false;

		if (dur >= LONG_PRESS_MS) {
			pr_info("adc-key: long %lld ms -> scheduling poweroff\n", dur);
			g_action_work.action = ACTION_POWEROFF;
		} else {
			pr_info("adc-key: short %lld ms -> scheduling reboot\n", dur);
			g_action_work.action = ACTION_REBOOT;
		}
		schedule_work(&g_action_work.work);
		return true;	/* 消费事件 */
	}
	return false;
}

/* ---- handler connect/disconnect（不变） ---- */
static int adc_key_connect(struct input_handler *handler,
			   struct input_dev *dev,
			   const struct input_device_id *id)
{
	struct input_handle *h;
	int ret;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h) return -ENOMEM;

	h->dev     = dev;
	h->handler = handler;
	h->name    = "adc-key-action";

	ret = input_register_handle(h);
	if (ret) { kfree(h); return ret; }

	ret = input_open_device(h);
	if (ret) { input_unregister_handle(h); kfree(h); return ret; }

	pr_info("adc-key-action: bound to %s\n", dev->name);
	return 0;
}

static void adc_key_disconnect(struct input_handle *h)
{
	input_close_device(h);
	input_unregister_handle(h);
	kfree(h);
}

static const struct input_device_id adc_key_ids[] = {
	{
		.flags  = INPUT_DEVICE_ID_MATCH_EVBIT |
			  INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit  = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(TARGET_KEY)] = BIT_MASK(TARGET_KEY) },
	},
	{ },
};
MODULE_DEVICE_TABLE(input, adc_key_ids);

static struct input_handler adc_key_handler = {
	.filter     = adc_key_filter,
	.connect    = adc_key_connect,
	.disconnect = adc_key_disconnect,
	.name       = "adc-key-action",
	.id_table   = adc_key_ids,
};

/* ---- init/exit ---- */
static int __init adc_key_init(void)
{
	INIT_WORK(&g_action_work.work, do_key_action);
	return input_register_handler(&adc_key_handler);
}

static void __exit adc_key_exit(void)
{
	input_unregister_handler(&adc_key_handler);
	cancel_work_sync(&g_action_work.work);
}

module_init(adc_key_init);
module_exit(adc_key_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Short press=reboot, long press=poweroff (workqueue safe)");
