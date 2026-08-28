package main

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"html/template"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/creack/pty"
	"golang.org/x/net/websocket"
)

//go:embed index.html
var indexHTML string

const uploadDir = "/tmp"
const blockSize = 4 * 1024 * 1024

// ============ JSON Helpers ============

func jsonOK(w http.ResponseWriter, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(data)
}

func jsonErr(w http.ResponseWriter, code int, msg string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(map[string]string{"error": msg})
}

// ============ Flash Task Management ============

type FlashTask struct {
	ID         string  `json:"id"`
	Status     string  `json:"status"`
	Progress   float64 `json:"progress"`
	Written    int64   `json:"written"`
	Total      int64   `json:"total"`
	Speed      float64 `json:"speed"`
	Error      string  `json:"error,omitempty"`
	SourceFile string  `json:"-"`
	TargetDev  string  `json:"-"`
}

var (
	tasks   = make(map[string]*FlashTask)
	tasksMu sync.RWMutex
	taskSeq int
)

func newTask(source, target string, total int64) *FlashTask {
	tasksMu.Lock()
	defer tasksMu.Unlock()
	taskSeq++
	id := fmt.Sprintf("task_%d_%d", time.Now().UnixNano(), taskSeq)
	t := &FlashTask{ID: id, Status: "writing", Total: total, SourceFile: source, TargetDev: target}
	tasks[id] = t
	return t
}

func getTask(id string) *FlashTask {
	tasksMu.RLock()
	defer tasksMu.RUnlock()
	return tasks[id]
}

func updateTask(id string, fn func(*FlashTask)) {
	tasksMu.Lock()
	defer tasksMu.Unlock()
	if t, ok := tasks[id]; ok {
		fn(t)
	}
}

// ============ Block Device Structures ============

type BlockDevice struct {
	Name  string `json:"name"`
	Path  string `json:"path"`
	Type  string `json:"type"`
	Size  string `json:"size"`
	Model string `json:"model"`
}

type LsblkOutput struct {
	BlockDevices []LsblkDevice `json:"blockdevices"`
}

// 【关键修复】
// Size 用 json.Number：兼容 lsblk 输出数字(128035675648)或字符串("128035675648")两种格式
// Model/Tran 用 *string 指针：兼容 null 值（虽然 string 也能接收 null，但指针更明确）
type LsblkDevice struct {
	Name  string       `json:"name"`
	Size  json.Number  `json:"size"`
	Type  string       `json:"type"`
	Model *string      `json:"model"`
	Tran  *string      `json:"tran"`
}

// ============ Main ============

func main() {
	tmpl := template.Must(template.New("index").Parse(indexHTML))

	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) { tmpl.Execute(w, nil) })
	http.HandleFunc("/api/devices", func(w http.ResponseWriter, r *http.Request) { jsonOK(w, getFilteredDevices()) })
	http.HandleFunc("/upload", handleUpload)
	http.HandleFunc("/api/progress", handleProgress)
	http.HandleFunc("/api/reboot", handleReboot)

	// ✅ WebSocket 终端端点
	http.Handle("/api/terminal", websocket.Handler(handleTerminal))

	fmt.Println("========================================")
	fmt.Println("  RKdev + Terminal port:80 protocol:http")
	fmt.Println("========================================")
	log.Fatal(http.ListenAndServe(":80", nil))
}

// ============ Terminal (PTY over WebSocket) ============

func handleTerminal(ws *websocket.Conn) {
	defer ws.Close()

	shell := os.Getenv("SHELL")
	if shell == "" {
		shell = "/bin/sh"
	}

	cmd := exec.Command(shell)
	cmd.Env = append(os.Environ(), "TERM=xterm-256color")

	ptmx, err := pty.Start(cmd)
	if err != nil {
		log.Printf("[TERM] PTY start failed: %v", err)
		ws.Write([]byte("\r\n*** Failed to start terminal: " + err.Error() + " ***\r\n"))
		return
	}
	defer ptmx.Close()

	pty.Setsize(ptmx, &pty.Winsize{Rows: 30, Cols: 120})

	done := make(chan struct{}, 2)

	go func() {
		defer func() { done <- struct{}{} }()
		buf := make([]byte, 4096)
		for {
			n, err := ws.Read(buf)
			if err != nil {
				return
			}
			if n > 0 && buf[0] == '0' {
				ptmx.Write(buf[1:n])
			} else if n >= 5 && buf[0] == '1' {
				rows := uint16(buf[1])<<8 | uint16(buf[2])
				cols := uint16(buf[3])<<8 | uint16(buf[4])
				pty.Setsize(ptmx, &pty.Winsize{Rows: rows, Cols: cols})
			}
		}
	}()

	go func() {
		defer func() { done <- struct{}{} }()
		buf := make([]byte, 4096)
		for {
			n, err := ptmx.Read(buf)
			if err != nil {
				return
			}
			if _, werr := ws.Write(buf[:n]); werr != nil {
				return
			}
		}
	}()

	<-done
	cmd.Process.Kill()
	cmd.Wait()
	log.Println("[TERM] Session ended")
}

// ============ Reboot ============

func handleReboot(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonErr(w, http.StatusMethodNotAllowed, "仅支持 POST")
		return
	}
	log.Println("[REBOOT] Requested")
	jsonOK(w, map[string]string{"message": "系统即将重启..."})
	go func() {
		time.Sleep(500 * time.Millisecond)
		exec.Command("/sbin/reboot").Run()
	}()
}

// ============ Upload & Flash ============

func handleUpload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		jsonErr(w, http.StatusMethodNotAllowed, "仅支持 POST")
		return
	}
	if err := r.ParseMultipartForm(32 << 20); err != nil {
		jsonErr(w, http.StatusBadRequest, "解析表单失败: "+err.Error())
		return
	}
	file, handler, err := r.FormFile("firmware")
	if err != nil {
		jsonErr(w, http.StatusBadRequest, "获取文件失败: "+err.Error())
		return
	}
	defer file.Close()

	target := r.FormValue("target_device")
	if target == "" || !strings.HasPrefix(target, "/dev/") || strings.Contains(target, "..") {
		jsonErr(w, http.StatusBadRequest, "非法目标设备路径")
		return
	}

	safeName := filepath.Base(handler.Filename)
	tmpPath := filepath.Join(uploadDir, fmt.Sprintf("fw_%d_%s", time.Now().UnixNano(), safeName))

	dst, err := os.Create(tmpPath)
	if err != nil {
		jsonErr(w, http.StatusInternalServerError, "创建临时文件失败: "+err.Error())
		return
	}
	written, err := io.Copy(dst, file)
	dst.Close()
	if err != nil {
		os.Remove(tmpPath)
		jsonErr(w, http.StatusInternalServerError, "保存文件失败: "+err.Error())
		return
	}

	log.Printf("[UPLOAD] %s (%d bytes) -> %s", tmpPath, written, target)
	task := newTask(tmpPath, target, written)
	go doFlash(task)

	jsonOK(w, map[string]string{
		"task_id": task.ID,
		"message": fmt.Sprintf("文件 %s (%d MB) 已暂存，正在刷写到 %s ...", safeName, written/1024/1024, target),
	})
}

func doFlash(task *FlashTask) {
	log.Printf("[%s] Flash: %s -> %s (%d bytes)", task.ID, task.SourceFile, task.TargetDev, task.Total)

	src, err := os.Open(task.SourceFile)
	if err != nil {
		updateTask(task.ID, func(t *FlashTask) { t.Status = "error"; t.Error = "打开源文件失败: " + err.Error() })
		return
	}
	defer src.Close()

	dst, err := os.OpenFile(task.TargetDev, os.O_WRONLY|os.O_SYNC, 0)
	if err != nil {
		updateTask(task.ID, func(t *FlashTask) { t.Status = "error"; t.Error = "打开目标设备失败: " + err.Error() })
		return
	}
	defer dst.Close()

	buf := make([]byte, blockSize)
	var totalWritten int64
	start := time.Now()

	for {
		n, readErr := src.Read(buf)
		if n > 0 {
			wn, writeErr := dst.Write(buf[:n])
			if writeErr != nil {
				updateTask(task.ID, func(t *FlashTask) {
					t.Status = "error"
					t.Error = fmt.Sprintf("写入失败 @ %d: %v", totalWritten, writeErr)
				})
				return
			}
			totalWritten += int64(wn)
			elapsed := time.Since(start).Seconds()
			updateTask(task.ID, func(t *FlashTask) {
				t.Written = totalWritten
				t.Progress = float64(totalWritten) / float64(task.Total) * 100
				t.Speed = float64(totalWritten) / elapsed
			})
		}
		if readErr != nil {
			if readErr != io.EOF {
				updateTask(task.ID, func(t *FlashTask) { t.Status = "error"; t.Error = "读取失败: " + readErr.Error() })
				return
			}
			break
		}
	}

	updateTask(task.ID, func(t *FlashTask) { t.Status = "syncing" })
	dst.Sync()

	elapsed := time.Since(start).Seconds()
	updateTask(task.ID, func(t *FlashTask) {
		t.Status = "done"; t.Progress = 100; t.Written = task.Total
		t.Speed = float64(task.Total) / elapsed
	})
	log.Printf("[%s] ✅ Done: %d bytes, %.1fs, %.1f MB/s", task.ID, totalWritten, elapsed, float64(totalWritten)/elapsed/1024/1024)
	os.Remove(task.SourceFile)
}

// ============ SSE Progress ============

func handleProgress(w http.ResponseWriter, r *http.Request) {
	taskID := r.URL.Query().Get("id")
	if taskID == "" {
		jsonErr(w, http.StatusBadRequest, "缺少 task id")
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.Header().Set("X-Accel-Buffering", "no")

	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "streaming unsupported", 500)
		return
	}
	for {
		task := getTask(taskID)
		if task == nil {
			fmt.Fprintf(w, "data: {\"status\":\"error\",\"error\":\"task not found\"}\n\n")
			flusher.Flush()
			return
		}
		data, _ := json.Marshal(task)
		fmt.Fprintf(w, "data: %s\n\n", data)
		flusher.Flush()
		if task.Status == "done" || task.Status == "error" {
			return
		}
		time.Sleep(200 * time.Millisecond)
	}
}

// ============ Device Filter ============

func getFilteredDevices() []BlockDevice {
	cmd := exec.Command("lsblk", "-bdnJ", "-o", "NAME,SIZE,TYPE,MODEL,TRAN")
	output, err := cmd.Output()
	if err != nil {
		log.Printf("lsblk command failed: %v", err)
		return getMockDevices()
	}

	var data LsblkOutput
	if err := json.Unmarshal(output, &data); err != nil {
		log.Printf("json unmarshal failed: %v, raw output: %s", err, string(output))
		return getMockDevices()
	}

	var result []BlockDevice
	for _, d := range data.BlockDevices {
		// 过滤掉 loop, ram, dm-, sr, zram 等虚拟/特殊设备
		if strings.HasPrefix(d.Name, "loop") || strings.HasPrefix(d.Name, "ram") ||
			strings.HasPrefix(d.Name, "dm-") || strings.HasPrefix(d.Name, "sr") ||
			strings.HasPrefix(d.Name, "zram") {
			continue
		}

		// 只保留 sd 和 nvme 设备
		if !strings.HasPrefix(d.Name, "sd") && !strings.HasPrefix(d.Name, "nvme") {
			continue
		}

		// 安全获取 tran 值（可能是 nil）
		tran := ""
		if d.Tran != nil {
			tran = *d.Tran
		}

		dt := "Disk"
		if strings.HasPrefix(d.Name, "nvme") {
			dt = "NVMe"
		} else if tran == "usb" {
			dt = "USB"
		} else if tran == "sata" || tran == "scsi" {
			dt = "SATA/SCSI"
		}

		// 【关键修复】json.Number 兼容数字和字符串，转成 int64
		sizeBytes, _ := d.Size.Int64()

		// 安全获取 model 值（可能是 nil）
		model := ""
		if d.Model != nil {
			model = *d.Model
		}

		result = append(result, BlockDevice{
			Name:  d.Name,
			Path:  "/dev/" + d.Name,
			Type:  dt,
			Size:  formatBytes(sizeBytes),
			Model: model,
		})
	}

	if len(result) == 0 {
		log.Println("No valid devices found, returning mock data")
		return getMockDevices()
	}

	return result
}

func getMockDevices() []BlockDevice {
	return []BlockDevice{
		{Name: "sda", Path: "/dev/sda", Type: "SATA", Size: "256 GB", Model: "Samsung SSD"},
		{Name: "nvme0n1", Path: "/dev/nvme0n1", Type: "NVMe", Size: "1 TB", Model: "WD Black"},
		{Name: "sdb", Path: "/dev/sdb", Type: "USB", Size: "32 GB", Model: "Generic USB"},
	}
}

func formatBytes(b int64) string {
	const unit = 1024
	if b <= 0 {
		return "0 B"
	}
	div, exp := int64(unit), 0
	for n := b / unit; n >= unit; n /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.1f %cB", float64(b)/float64(div), "kMGTPE"[exp])
}
