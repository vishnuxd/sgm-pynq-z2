import os
import cv2
import numpy as np
import threading
import time
from flask import Flask, request, render_template_string, jsonify, send_file
from werkzeug.utils import secure_filename
from pynq import allocate, Overlay

# ============================================================================
# FPGA OVERLAY
# ============================================================================
BITSTREAM_PATH = '/home/xilinx/jupyter_notebooks/sgm/sgm_v9.bit'

try:
    overlay = Overlay(BITSTREAM_PATH)
    print(f"[OK] Overlay loaded: {BITSTREAM_PATH}")
except Exception as e:
    print(f"[WARN] Could not load overlay: {e}")

app = Flask(__name__)
app.config['MAX_CONTENT_LENGTH'] = 200 * 1024 * 1024

UPLOAD_FOLDER = 'uploads'
OUTPUT_FOLDER = 'outputs'
os.makedirs(UPLOAD_FOLDER, exist_ok=True)
os.makedirs(OUTPUT_FOLDER, exist_ok=True)

# Global state
progress = {
    'status': 'idle',
    'percent': 0,
    'message': 'Ready',
    'result_file': None,
    'input_file': None,
    'stats': {}
}
progress_lock = threading.Lock()

# ============================================================================
# HTML TEMPLATE
# ============================================================================
HTML = """
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>4-Path SGM Stereo Depth Estimation</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            background: #0d1117;
            color: #c9d1d9;
            font-family: 'Segoe UI', 'Helvetica Neue', Arial, sans-serif;
            padding: 30px 20px;
            min-height: 100vh;
        }
        .container { max-width: 1200px; margin: 0 auto; }

        header {
            text-align: center;
            border-bottom: 1px solid #30363d;
            padding-bottom: 20px;
            margin-bottom: 30px;
        }
        h1 {
            color: #58a6ff;
            font-size: 26px;
            font-weight: 600;
            letter-spacing: 0.5px;
        }
        .subtitle {
            color: #8b949e;
            font-size: 14px;
            margin-top: 6px;
        }

        .upload-box {
            border: 2px dashed #30363d;
            border-radius: 8px;
            padding: 40px 20px;
            margin: 0 auto 20px;
            max-width: 600px;
            background: #161b22;
            cursor: pointer;
            transition: all 0.2s;
            text-align: center;
        }
        .upload-box:hover { border-color: #58a6ff; background: #1c2128; }
        .upload-box.dragover { border-color: #3fb950; background: #1a2a1f; }
        .upload-box .icon {
            font-size: 36px;
            color: #58a6ff;
            margin-bottom: 12px;
        }
        .upload-box p { font-size: 15px; color: #c9d1d9; margin: 4px 0; }
        .upload-box .hint { font-size: 12px; color: #6e7681; }
        input[type=file] { display: none; }

        .btn {
            background: #238636;
            color: #ffffff;
            border: none;
            padding: 10px 28px;
            border-radius: 6px;
            font-size: 14px;
            font-weight: 600;
            cursor: pointer;
            display: block;
            margin: 0 auto;
            transition: background 0.2s;
            letter-spacing: 0.3px;
        }
        .btn:hover:not(:disabled) { background: #2ea043; }
        .btn:disabled { background: #21262d; color: #484f58; cursor: not-allowed; }

        .progress-wrap {
            width: 600px;
            max-width: 100%;
            margin: 24px auto;
            display: none;
        }
        .progress-bar {
            height: 8px;
            background: #21262d;
            border-radius: 4px;
            overflow: hidden;
        }
        .progress-fill {
            height: 100%;
            background: linear-gradient(90deg, #1f6feb, #3fb950);
            width: 0%;
            transition: width 0.3s;
        }
        .progress-label {
            display: flex;
            justify-content: space-between;
            font-size: 12px;
            color: #8b949e;
            margin-top: 8px;
        }

        .status {
            text-align: center;
            font-size: 13px;
            color: #8b949e;
            margin: 16px 0;
            min-height: 20px;
        }

        .video-row {
            display: flex;
            justify-content: center;
            gap: 24px;
            margin-top: 30px;
            flex-wrap: wrap;
        }
        .video-card {
            background: #161b22;
            border: 1px solid #30363d;
            border-radius: 8px;
            padding: 16px;
        }
        .video-card .label {
            font-size: 13px;
            color: #58a6ff;
            font-weight: 600;
            margin-bottom: 10px;
            text-transform: uppercase;
            letter-spacing: 0.5px;
        }
        video {
            width: 480px;
            height: 360px;
            display: block;
            background: #000;
            border-radius: 4px;
        }

        .stats-table {
            margin: 40px auto 0;
            max-width: 700px;
            background: #161b22;
            border: 1px solid #30363d;
            border-radius: 8px;
            overflow: hidden;
        }
        .stats-table h3 {
            padding: 14px 20px;
            font-size: 13px;
            color: #c9d1d9;
            font-weight: 600;
            text-transform: uppercase;
            letter-spacing: 0.5px;
            background: #1c2128;
            border-bottom: 1px solid #30363d;
        }
        .stats-table table {
            width: 100%;
            border-collapse: collapse;
            font-size: 13px;
        }
        .stats-table td {
            padding: 10px 20px;
            border-bottom: 1px solid #21262d;
        }
        .stats-table td:first-child { color: #8b949e; }
        .stats-table td:last-child {
            text-align: right;
            color: #58a6ff;
            font-family: 'Consolas', 'Monaco', monospace;
            font-weight: 500;
        }
        .stats-table tr:last-child td {
            border-bottom: none;
            font-weight: 600;
            color: #c9d1d9;
        }
        .stats-table tr:last-child td:last-child { color: #3fb950; }
    </style>
</head>
<body>
<div class="container">
    <header>
        <h1>4-Path SGM Stereo Depth Estimation</h1>
        <div class="subtitle">PYNQ-Z2 FPGA Accelerator &mdash; Hardware-Accelerated Stereo Vision Pipeline</div>
    </header>

    <div class="upload-box" id="dropZone">
        <div class="icon">&#9633;</div>
        <p>Drop a stereo video file here, or click to browse</p>
        <p class="hint">Supported: side-by-side stereo video (left camera | right camera)</p>
        <input type="file" id="fileInput" accept="video/*">
    </div>

    <button class="btn" id="processBtn" disabled>Process with FPGA</button>

    <div class="progress-wrap" id="progressWrap">
        <div class="progress-bar">
            <div class="progress-fill" id="progressFill"></div>
        </div>
        <div class="progress-label">
            <span id="statusMsg">Initializing...</span>
            <span id="percentLabel">0%</span>
        </div>
    </div>

    <div class="video-row" id="videoRow" style="display:none;">
        <div class="video-card">
            <div class="label">Input &mdash; Stereo Pair</div>
            <video id="inputVideo" controls autoplay loop muted></video>
        </div>
        <div class="video-card">
            <div class="label">Output &mdash; Depth Map (FPGA)</div>
            <video id="depthVideo" controls autoplay loop muted></video>
        </div>
    </div>

    <div class="stats-table" id="statsTable" style="display:none;">
        <h3>Processing Statistics</h3>
        <table id="statsBody">
            <tr><td>Status</td><td>Processing...</td></tr>
        </table>
    </div>
</div>

<script>
    const dropZone = document.getElementById('dropZone');
    const fileInput = document.getElementById('fileInput');
    const processBtn = document.getElementById('processBtn');
    const progressWrap = document.getElementById('progressWrap');
    const progressFill = document.getElementById('progressFill');
    const statusMsg = document.getElementById('statusMsg');
    const percentLabel = document.getElementById('percentLabel');
    const videoRow = document.getElementById('videoRow');
    const statsTable = document.getElementById('statsTable');
    const statsBody = document.getElementById('statsBody');
    let selectedFile = null;

    dropZone.addEventListener('click', () => fileInput.click());
    dropZone.addEventListener('dragover', (e) => { e.preventDefault(); dropZone.classList.add('dragover'); });
    dropZone.addEventListener('dragleave', () => dropZone.classList.remove('dragover'));
    dropZone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropZone.classList.remove('dragover');
        if (e.dataTransfer.files.length) {
            fileInput.files = e.dataTransfer.files;
            handleFile(e.dataTransfer.files[0]);
        }
    });
    fileInput.addEventListener('change', (e) => {
        if (e.target.files.length) handleFile(e.target.files[0]);
    });

    function handleFile(file) {
        selectedFile = file;
        dropZone.querySelector('p').textContent = 'Selected: ' + file.name;
        processBtn.disabled = false;
    }

    processBtn.addEventListener('click', async () => {
        if (!selectedFile) return;
        processBtn.disabled = true;
        progressWrap.style.display = 'block';
        videoRow.style.display = 'none';
        statsTable.style.display = 'none';
        statusMsg.textContent = 'Uploading...';

        const formData = new FormData();
        formData.append('video', selectedFile);

        try {
            const res = await fetch('/upload', { method: 'POST', body: formData });
            const data = await res.json();
            if (data.error) { alert('Error: ' + data.error); return; }
            pollProgress();
        } catch (e) {
            alert('Upload failed: ' + e);
        }
    });

    async function pollProgress() {
        const res = await fetch('/progress');
        const data = await res.json();
        progressFill.style.width = data.percent + '%';
        percentLabel.textContent = data.percent + '%';
        statusMsg.textContent = data.message;

        if (data.status === 'done') {
            progressFill.style.width = '100%';
            percentLabel.textContent = '100%';
            statusMsg.textContent = 'Processing complete';
            showResult(data.input_file, data.result_file, data.stats);
        } else if (data.status === 'error') {
            statusMsg.textContent = 'Error: ' + data.message;
            processBtn.disabled = false;
        } else {
            setTimeout(pollProgress, 500);
        }
    }

    function showResult(inputFile, depthFile, stats) {
        videoRow.style.display = 'flex';
        const ts = Date.now();
        document.getElementById('inputVideo').src = '/outputs/' + inputFile + '?t=' + ts;
        document.getElementById('depthVideo').src = '/outputs/' + depthFile + '?t=' + ts;

        const inp = document.getElementById('inputVideo');
        const dep = document.getElementById('depthVideo');
        inp.addEventListener('play', () => dep.play());
        inp.addEventListener('pause', () => dep.pause());
        inp.addEventListener('seeked', () => { dep.currentTime = inp.currentTime; });

        statsTable.style.display = 'block';
        statsBody.innerHTML = '';
        const rows = [
            ['Frame resolution', stats.resolution],
            ['Total frames processed', stats.frames],
            ['Video duration', stats.duration],
            ['Preprocessing (CPU)', stats.preprocess],
            ['FPGA processing (SGM)', stats.fpga],
            ['Video encoding (FFmpeg)', stats.encode],
            ['FPGA throughput', stats.fpga_fps],
            ['Total throughput', stats.total_fps],
            ['Total wall-clock time', stats.total]
        ];
        rows.forEach(([k, v]) => {
            const tr = document.createElement('tr');
            tr.innerHTML = `<td>${k}</td><td>${v}</td>`;
            statsBody.appendChild(tr);
        });
    }
</script>
</body>
</html>
"""

# ============================================================================
# ROUTES
# ============================================================================
@app.route('/')
def index():
    return render_template_string(HTML)

@app.route('/upload', methods=['POST'])
def upload():
    global progress
    if 'video' not in request.files:
        return jsonify({'error': 'No file uploaded'}), 400

    f = request.files['video']
    if not f.filename:
        return jsonify({'error': 'Empty filename'}), 400

    filename = secure_filename(f.filename)
    filepath = os.path.join(UPLOAD_FOLDER, filename)
    f.save(filepath)

    with progress_lock:
        progress = {
            'status': 'processing', 'percent': 0,
            'message': 'Starting...', 'result_file': None,
            'input_file': None, 'stats': {}
        }

    threading.Thread(target=process_video, args=(filepath, filename), daemon=True).start()
    return jsonify({'status': 'started', 'filename': filename})

@app.route('/progress')
def get_progress():
    with progress_lock:
        return jsonify(dict(progress))

@app.route('/outputs/<filename>')
def serve_output(filename):
    path = os.path.join(OUTPUT_FOLDER, filename)
    if filename.endswith('.mp4'):
        return send_file(path, mimetype='video/mp4', conditional=True)
    return send_file(path)

# ============================================================================
# PROCESSING PIPELINE with timing
# ============================================================================
def process_video(filepath, filename):
    global progress
    t_start = time.time()
    try:
        # ---- Step 1: Read video ----
        with progress_lock:
            progress['message'] = 'Reading video...'
            progress['percent'] = 5

        cap = cv2.VideoCapture(filepath)
        total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        fps_in = cap.get(cv2.CAP_PROP_FPS) or 10
        if fps_in < 1 or fps_in > 60:
            fps_in = 10
        if total == 0:
            total = 999

        # ---- Step 2: Preprocess ----
        t_pre_start = time.time()
        with progress_lock:
            progress['message'] = f'Preprocessing {total} frames...'
            progress['percent'] = 10

        with open('frames.bin', 'wb') as f:
            i = 0
            while True:
                ret, frame = cap.read()
                if not ret:
                    break
                h, w = frame.shape[:2]
                left = cv2.resize(
                    cv2.cvtColor(frame[:, :w//2], cv2.COLOR_BGR2GRAY),
                    (160, 120)
                )
                right = cv2.resize(
                    cv2.cvtColor(frame[:, w//2:], cv2.COLOR_BGR2GRAY),
                    (160, 120)
                )
                f.write(left.tobytes())
                f.write(right.tobytes())
                i += 1
        cap.release()
        N = i
        t_pre_end = time.time()

        with progress_lock:
            progress['message'] = f'Preprocessed {N} frames'
            progress['percent'] = 25

        # ---- Step 3: CMA allocation ----
        with progress_lock:
            progress['message'] = 'Allocating CMA buffers...'
            progress['percent'] = 28

        lb = allocate(shape=(120, 160), dtype=np.uint8)
        rb = allocate(shape=(120, 160), dtype=np.uint8)
        db = allocate(shape=(120, 160), dtype=np.uint8)

        # ---- Step 4: FPGA processing ----
        t_fpga_start = time.time()
        with progress_lock:
            progress['message'] = f'FPGA processing {N} frames...'
            progress['percent'] = 30

        cmd = (f"sudo ./sgm_driver {lb.physical_address} {rb.physical_address} "
               f"{db.physical_address} {N} frames.bin result.bin")
        ret = os.system(cmd)

        t_fpga_end = time.time()

        if ret != 0:
            with progress_lock:
                progress['status'] = 'error'
                progress['message'] = 'C driver failed (check terminal)'
            return

        with progress_lock:
            progress['message'] = 'Building output video...'
            progress['percent'] = 80

        # ---- Step 5: Encode ----
        t_enc_start = time.time()
        result = np.fromfile('result.bin', dtype=np.uint8).reshape(N, 120, 160)

        base_name = os.path.splitext(filename)[0]
        depth_out_name = 'depth_' + base_name + '.mp4'
        input_out_name = 'input_' + base_name + '.mp4'

        depth_out_path = os.path.join(OUTPUT_FOLDER, depth_out_name)
        input_out_path = os.path.join(OUTPUT_FOLDER, input_out_name)

        tmp_depth = os.path.join(OUTPUT_FOLDER, 'tmp_depth.mp4')
        tmp_input = os.path.join(OUTPUT_FOLDER, 'tmp_input.mp4')

        fourcc = cv2.VideoWriter_fourcc(*'mp4v')

        # Raw depth
        out = cv2.VideoWriter(tmp_depth, fourcc, fps_in, (640, 480))
        for i in range(N):
            d = result[i]
            d_scaled = (d.astype(np.float32) / max(d.max(), 1) * 255).astype(np.uint8)
            d_color = cv2.applyColorMap(d_scaled, cv2.COLORMAP_TURBO)
            d_big = cv2.resize(d_color, (640, 480), interpolation=cv2.INTER_NEAREST)
            out.write(d_big)
        out.release()

        # Raw input
        cap = cv2.VideoCapture(filepath)
        inp_writer = cv2.VideoWriter(tmp_input, fourcc, fps_in, (640, 480))
        while True:
            ret, frame = cap.read()
            if not ret:
                break
            inp_writer.write(cv2.resize(frame, (640, 480)))
        cap.release()
        inp_writer.release()

        # FFmpeg H.264 conversion
        with progress_lock:
            progress['message'] = 'Encoding for browser...'
            progress['percent'] = 90

        for src, dst in [(tmp_depth, depth_out_path), (tmp_input, input_out_path)]:
            ff_cmd = (f"ffmpeg -y -i {src} -c:v libx264 -preset ultrafast "
                      f"-pix_fmt yuv420p -movflags +faststart {dst} "
                      f"> /dev/null 2>&1")
            os.system(ff_cmd)
            if os.path.exists(src):
                os.remove(src)

        t_enc_end = time.time()
        t_end = time.time()

        # ---- Timing stats ----
        duration_sec = N / fps_in
        t_pre = t_pre_end - t_pre_start
        t_fpga = t_fpga_end - t_fpga_start
        t_enc = t_enc_end - t_enc_start
        t_total = t_end - t_start
        throughput = N / t_total if t_total > 0 else 0
        fpga_throughput = N / t_fpga if t_fpga > 0 else 0

        stats = {
            'resolution': '160 x 120',
            'frames': f'{N}',
            'duration': f'{duration_sec:.2f} s',
            'preprocess': f'{t_pre:.2f} s',
            'fpga': f'{t_fpga:.2f} s',
            'encode': f'{t_enc:.2f} s',
            'fpga_fps': f'{fpga_throughput:.2f} FPS',
            'total_fps': f'{throughput:.2f} FPS',
            'total': f'{t_total:.2f} s'
        }

        with progress_lock:
            progress['status'] = 'done'
            progress['percent'] = 100
            progress['message'] = f'Completed: {N} frames processed'
            progress['result_file'] = depth_out_name
            progress['input_file'] = input_out_name
            progress['stats'] = stats

    except Exception as e:
        with progress_lock:
            progress['status'] = 'error'
            progress['message'] = str(e)

# ============================================================================
# MAIN
# ============================================================================
if __name__ == '__main__':
    print("\n" + "=" * 60)
    print("4-PATH SGM - WEB DEMO SERVER")
    print("=" * 60)
    print("Open in browser:  http://<PYNQ_IP>:5000")
    print("=" * 60 + "\n")
    app.run(host='0.0.0.0', port=5000, threaded=True)