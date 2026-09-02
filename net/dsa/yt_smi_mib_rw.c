#if 1
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

#define PROC_BUF_SIZE (64 * 1024)

/* 保存两颗芯片的 priv 指针 */
static struct yt921x_priv *yt921x_units[2] = {NULL, NULL};

static char *smi_out_buf;
static int smi_out_len = 0;

static char *mib_out_buf;
static int mib_out_len = 0;

static void smi_buf_printf(const char *fmt, ...)
{
    va_list args;
    int len;

    if (!smi_out_buf || smi_out_len >= PROC_BUF_SIZE - 1)
        return;

    va_start(args, fmt);
    len = vsnprintf(smi_out_buf + smi_out_len, PROC_BUF_SIZE - smi_out_len, fmt, args);
    va_end(args);

    if (len > 0)
        smi_out_len += len;
}

static void mib_buf_printf(const char *fmt, ...)
{
    va_list args;
    int len;

    if (!mib_out_buf || mib_out_len >= PROC_BUF_SIZE - 1)
        return;

    va_start(args, fmt);
    len = vsnprintf(mib_out_buf + mib_out_len, PROC_BUF_SIZE - mib_out_len, fmt, args);
    va_end(args);

    if (len > 0)
        mib_out_len += len;
}

/* 直接调用原生 yt921x_reg_write，自带加锁与防冲突 */
static int yt_smi_switch_write(u8 unit, u32 reg_addr, u32 reg_value)
{
    int ret;
    struct yt921x_priv *priv;

    if (unit >= 2 || !yt921x_units[unit]) {
        smi_buf_printf("Error: stmmac-%u is not ready/probed\n", unit);
        return -ENODEV;
    }
    priv = yt921x_units[unit];

    mutex_lock(&priv->reg_lock);
    ret = yt921x_reg_write(priv, reg_addr, reg_value);
    mutex_unlock(&priv->reg_lock);

    return ret;
}

/* 直接调用原生 yt921x_reg_read，自带加锁与防冲突 */
static int yt_smi_switch_read(u8 unit, u32 reg_addr, u32 *reg_value)
{
    int ret;
    struct yt921x_priv *priv;

    if (unit >= 2 || !yt921x_units[unit]) {
        smi_buf_printf("Error: stmmac-%u is not ready/probed\n", unit);
        return -ENODEV;
    }
    priv = yt921x_units[unit];

    mutex_lock(&priv->reg_lock);
    ret = yt921x_reg_read(priv, reg_addr, reg_value);
    mutex_unlock(&priv->reg_lock);

    return ret;
}

static void yt_smi_switch_rmw(u8 unit, u32 reg, u32 mask, u32 set)
{ 
    u32 val = 0; 
    yt_smi_switch_read(unit, reg, &val); 
    val &= ~mask;
    val |= set;
    yt_smi_switch_write(unit, reg, val); 
}

static void yt_smi_dump_range(u8 unit, const char *name, u32 start, u32 end)
{
    u32 addr, val = 0;
    smi_buf_printf("==== [stmmac-%u] Dump %s (0x%08x ~ 0x%08x) ====\n", unit, name, start, end);
    for (addr = start; addr <= end; addr += 4) {
        val = 0;
        yt_smi_switch_read(unit, addr, &val);
        smi_buf_printf("stmmac-%u [0x%08x] = 0x%08x\n", unit, addr, val);
        if ((addr - start) % 0x100 == 0)
            cond_resched();
    }
}

static void yt_smi_dump_all(u8 unit)
{
    int p;
    yt_smi_dump_range(unit, "Global System", 0x80000, 0x80080);
    yt_smi_dump_range(unit, "L2 / VLAN",     0x90000, 0x90100);
    yt_smi_dump_range(unit, "QoS / Queue",   0xB0000, 0xB0080);
    yt_smi_dump_range(unit, "MIB Control",   0xC0000, 0xC0020);
    for (p = 0; p <= 9; p++) {
        char p_name[32];
        snprintf(p_name, sizeof(p_name), "Port %d MAC", p);
        yt_smi_dump_range(unit, p_name, 0xD0000 + (p * 0x100), 0xD0030 + (p * 0x100));
    }
    smi_buf_printf("==== [stmmac-%u] Dump All Completed ====\n", unit);
}

static ssize_t smi_read_proc(struct file *filp, char __user *buffer, size_t count, loff_t *offp)
{
    return simple_read_from_buffer(buffer, count, offp, smi_out_buf, smi_out_len);
}

static ssize_t smi_write_proc(struct file *filp, const char *buffer, size_t count, loff_t *offp)
{
    char *str, *cmd, *value; 
    char tmpbuf[128] = {0}; 
    uint8_t unit = 0;
    uint32_t regAddr = 0, regData = 0, rData = 0;
    
    if (count >= sizeof(tmpbuf)) 
        goto error;
        
    if (!buffer || copy_from_user(tmpbuf, buffer, count) != 0)
        return 0;
        
    smi_out_len = 0;

    if (count > 0)
    {
        str = tmpbuf; 
        cmd = strsep(&str, "\t \n");
        if (!cmd) goto error;
        
        if (strcmp(cmd, "write") == 0)
        { 
            value = strsep(&str, "\t \n");
            if (!value) goto error;
            unit = simple_strtoul(value, NULL, 10);

            value = strsep(&str, "\t \n");
            if (!value) goto error;
            regAddr = simple_strtoul(value, NULL, 16); 
            
            value = strsep(&str, "\t \n");
            if (!value) goto error;
            regData = simple_strtoul(value, NULL, 16); 

            yt_smi_switch_write(unit, regAddr, regData);
            smi_buf_printf("stmmac-%u: write regAddr = 0x%08x, regData = 0x%08x (OK)\n", unit, regAddr, regData);
        }
        else if (strcmp(cmd, "read") == 0)
        { 
            value = strsep(&str, "\t \n");
            if (!value) goto error;
            unit = simple_strtoul(value, NULL, 10);

            value = strsep(&str, "\t \n");
            if (!value) goto error;
            regAddr = simple_strtoul(value, NULL, 16); 

            yt_smi_switch_read(unit, regAddr, &rData);
            smi_buf_printf("stmmac-%u: read regAddr = 0x%08x, regData = 0x%08x\n", unit, regAddr, rData);
        }
        else if (strcmp(cmd, "dump") == 0)
        {
            value = strsep(&str, "\t \n");
            if (!value) goto error;
            unit = simple_strtoul(value, NULL, 10);

            value = strsep(&str, "\t \n");
            if (!value) goto error;

            if (strcmp(value, "all") == 0) {
                yt_smi_dump_all(unit);
            } else {
                uint32_t startAddr = simple_strtoul(value, NULL, 16);
                uint32_t endAddr = 0;

                value = strsep(&str, "\t \n");
                if (!value) goto error;
                endAddr = simple_strtoul(value, NULL, 16);

                yt_smi_dump_range(unit, "Custom Range", startAddr, endAddr);
            }
        }
        else 
        { 
            goto error; 
        }
    }
    return count; 

error: 
    smi_buf_printf("Usage:\n"
                   "  echo read <unit> <regaddr> > /proc/smi\n"
                   "  echo write <unit> <regaddr> <regdata> > /proc/smi\n"
                   "  echo dump <unit> all > /proc/smi\n"
                   "  echo dump <unit> <startaddr> <endaddr> > /proc/smi\n");
    return count; 
}

static struct proc_dir_entry *smi_proc; 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops smi_proc_fops = {
    .proc_write = smi_write_proc,
    .proc_read  = smi_read_proc,
    .proc_lseek = default_llseek,
};
#else
static const struct file_operations smi_proc_fops = {
    .write = smi_write_proc,
    .read  = smi_read_proc,
    .llseek = default_llseek,
};
#endif

/* MIB 计数部分 */
struct stat_mib_counter { 
    unsigned int size; 
    unsigned int offset; 
    const char *name; 
}; 

static const struct stat_mib_counter stat_mib[] = {
    { 1, 0x00, "RxBcast"}, 
    { 1, 0x04, "RxPause"}, 
    { 1, 0x08, "RxMcast"}, 
    { 1, 0x0C, "RxCrcErr"}, 
    { 1, 0x10, "RxAlignErr"}, 
    { 1, 0x14, "RxRunt"}, 
    { 1, 0x18, "RxFragment"}, 
    { 1, 0x1C, "RxSz64"}, 
    { 1, 0x20, "RxSz65To127"}, 
    { 1, 0x24, "RxSz128To255"}, 
    { 1, 0x28, "RxSz256To511"}, 
    { 1, 0x2C, "RxSz512To1023"}, 
    { 1, 0x30, "RxSz1024To1518"}, 
    { 1, 0x34, "RxJumbo"}, 
    { 2, 0x3C, "RxOkByte"}, 
    { 2, 0x44, "RxNoOkByte"}, 
    { 1, 0x4C, "RxOverFlow"}, 
    { 1, 0x54, "TxBcast"}, 
    { 1, 0x58, "TxPause"}, 
    { 1, 0x5C, "TxMcast"}, 
    { 1, 0x64, "TxSz64"}, 
    { 1, 0x68, "TxSz65To127"}, 
    { 1, 0x6C, "TxSz128To255"}, 
    { 1, 0x70, "TxSz256To511"}, 
    { 1, 0x74, "TxSz512To1023"}, 
    { 1, 0x78, "TxSz1024To1518"}, 
    { 1, 0x7C, "TxJumbo"}, 
    { 1, 0x80, "TxOverSize"}, 
    { 2, 0x84, "TxOkByte"}, 
    { 1, 0x8C, "TxCollision"}, 
    { 1, 0xA4, "TxLateCollision"}, 
}; 

#define YT9215_PORT_MIB_BASE(n) (0xc0100 + (n) * 0x100) 

static u32 stat_mib_port_get(u8 unit, u32 port)
{
    int i = 0; 
    u32 lowData = 0, highData = 0; 
    u64 resultData = 0, count = 0; 
    int mibCount = ARRAY_SIZE(stat_mib); 
    
    mib_buf_printf("==== [stmmac-%u] MIB Counters for Port %u ====\n", unit, port); 
    for (i = 0; i < mibCount; i++)
    {
        count = 0; 
        yt_smi_switch_read(unit, YT9215_PORT_MIB_BASE(port) + stat_mib[i].offset, &lowData); 
        count = lowData;
        
        if (stat_mib[i].size == 2)
        { 
            yt_smi_switch_read(unit, YT9215_PORT_MIB_BASE(port) + stat_mib[i].offset + 4, &highData); 
            resultData = highData; 
            count |= resultData << 32; 
        }
        
        if (stat_mib[i].size == 1) 
            mib_buf_printf("%-20s %20u\n", stat_mib[i].name, (u32)count); 
        else
            mib_buf_printf("%-20s %20llu\n", stat_mib[i].name, count); 
    }
    return 0; 
}

static ssize_t mib_read_proc(struct file *filp, char __user *buffer, size_t count, loff_t *offp)
{
    return simple_read_from_buffer(buffer, count, offp, mib_out_buf, mib_out_len);
}

static ssize_t mib_write_proc(struct file *filp, const char *buffer, size_t count, loff_t *offp)
{
    char *str, *cmd, *value; 
    char tmpbuf[128] = {0}; 
    uint32_t port = 0; 
    uint8_t unit = 0;
    
    if (count >= sizeof(tmpbuf)) 
        goto error;
        
    if (!buffer || copy_from_user(tmpbuf, buffer, count) != 0)
        return 0;
        
    mib_out_len = 0;

    if (count > 0)
    {
        str = tmpbuf; 
        cmd = strsep(&str, "\t \n");
        if (!cmd || strcmp(cmd, "mib") != 0) 
            goto error;

        value = strsep(&str, "\t \n");
        if (!value) goto error;
        unit = simple_strtoul(value, NULL, 10);

        cmd = strsep(&str, "\t \n");
        if (!cmd) goto error;

        if (strcmp(cmd, "enable") == 0)
        { 
            yt_smi_switch_rmw(unit, 0x80004, 1<<1, 1<<1); 
            mib_buf_printf("stmmac-%u: MIB enabled (OK)\n", unit);
        }
        else if (strcmp(cmd, "disable") == 0)
        { 
            yt_smi_switch_rmw(unit, 0x80004, 1<<1, 0<<1); 
            mib_buf_printf("stmmac-%u: MIB disabled (OK)\n", unit);
        }
        else if (strcmp(cmd, "clear") == 0)
        { 
            yt_smi_switch_write(unit, 0xc0004, 0<<0);
            yt_smi_switch_write(unit, 0xc0004, 1<<30); 
            mib_buf_printf("stmmac-%u: MIB counters cleared (OK)\n", unit);
        }
        else if (strcmp(cmd, "get") == 0)
        { 
            value = strsep(&str, "\t \n");
            if (!value) goto error;
            port = simple_strtoul(value, NULL, 10);
            if (port <= 9)
                stat_mib_port_get(unit, port); 
        }
        else 
        { 
            goto error; 
        }
    }
    return count; 

error: 
    mib_buf_printf("Usage:\n"
                   "  echo mib <unit> enable     > /proc/mib\n"
                   "  echo mib <unit> disable    > /proc/mib\n"
                   "  echo mib <unit> clear      > /proc/mib\n"
                   "  echo mib <unit> get <port> > /proc/mib\n");
    return count; 
}

static struct proc_dir_entry *mib_proc; 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops mib_proc_fops = {
    .proc_write = mib_write_proc,
    .proc_read  = mib_read_proc,
    .proc_lseek = default_llseek,
};
#else
static const struct file_operations mib_proc_fops = {
    .write = mib_write_proc,
    .read  = mib_read_proc,
    .llseek = default_llseek,
};
#endif

void smi_mib_proc_test(void)
{ 
    if (!smi_out_buf)
        smi_out_buf = vmalloc(PROC_BUF_SIZE);
    if (!mib_out_buf)
        mib_out_buf = vmalloc(PROC_BUF_SIZE);

    smi_proc = proc_create("smi", 0666, NULL, &smi_proc_fops); 
    mib_proc = proc_create("mib", 0666, NULL, &mib_proc_fops); 
}
#endif
