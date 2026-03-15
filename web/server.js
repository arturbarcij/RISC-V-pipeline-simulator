const http = require('http');
const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const PORT = 3000;
const ROOT = path.resolve(__dirname, '..');

const EXEC_OPTS = {
    timeout: 10000,
    encoding: 'utf8',
    stdio: ['pipe', 'pipe', 'pipe'],
    windowsHide: true          // <-- suppress CMD window flash on Windows
};

const SINGLE_EXE = path.join(ROOT, 'program_single.exe');
const PIPE_EXE   = path.join(ROOT, 'program.exe');

// Use execFileSync instead of execSync to avoid shell spawning a visible window
function runSimulator(exe, binPath) {
    const out = execFileSync(exe, [binPath, '--json'], EXEC_OPTS);
    // JSON line is the last line of stdout
    const lines = out.trim().split('\n');
    return JSON.parse(lines[lines.length - 1]);
}

function collectBenchmarks() {
    const testDirs = ['test_files/T1', 'test_files/T2', 'test_files/T3', 'test_files/T4'];
    const results = [];

    for (const dir of testDirs) {
        const fullDir = path.join(ROOT, dir);
        if (!fs.existsSync(fullDir)) continue;
        const files = fs.readdirSync(fullDir).filter(f => f.endsWith('.bin')).sort();

        for (const file of files) {
            const binPath = path.join(ROOT, dir, file);
            const name = path.basename(file, '.bin');
            const category = path.basename(dir);

            try {
                const single   = runSimulator(SINGLE_EXE, binPath);
                const pipeline = runSimulator(PIPE_EXE, binPath);
                results.push({ name, category, single, pipeline });
            } catch (e) {
                console.error(`Error running ${name}: ${e.message}`);
            }
        }
    }
    return results;
}

function runTest(testPath) {
    const binPath = path.join(ROOT, testPath);
    if (!fs.existsSync(binPath)) return { error: 'File not found' };

    try {
        return {
            single:   runSimulator(SINGLE_EXE, binPath),
            pipeline: runSimulator(PIPE_EXE, binPath)
        };
    } catch (e) {
        return { error: e.message };
    }
}

function listTests() {
    const testDirs = ['test_files/T1', 'test_files/T2', 'test_files/T3', 'test_files/T4'];
    const tests = [];
    for (const dir of testDirs) {
        const fullDir = path.join(ROOT, dir);
        if (!fs.existsSync(fullDir)) continue;
        const files = fs.readdirSync(fullDir).filter(f => f.endsWith('.bin')).sort();
        for (const file of files) {
            tests.push({
                name: path.basename(file, '.bin'),
                category: path.basename(dir),
                path: path.join(dir, file).replace(/\\/g, '/')
            });
        }
    }
    return tests;
}

// Pre-cache benchmarks at startup so first page load is instant
let cachedBenchmarks = null;
console.log('Pre-computing benchmarks...');
try {
    cachedBenchmarks = collectBenchmarks();
    console.log(`Cached ${cachedBenchmarks.length} benchmark results.`);
} catch (e) {
    console.error('Failed to pre-cache benchmarks:', e.message);
}

const server = http.createServer((req, res) => {
    const url = new URL(req.url, `http://localhost:${PORT}`);

    if (url.pathname === '/api/benchmarks') {
        // Use cache if available, otherwise re-collect
        const refresh = url.searchParams.get('refresh') === '1';
        if (refresh || !cachedBenchmarks) {
            cachedBenchmarks = collectBenchmarks();
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(cachedBenchmarks));
        return;
    }

    if (url.pathname === '/api/tests') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(listTests()));
        return;
    }

    if (url.pathname === '/api/run') {
        const testPath = url.searchParams.get('test');
        if (!testPath) {
            res.writeHead(400, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: 'Missing ?test= parameter' }));
            return;
        }
        const result = runTest(testPath);
        // Update cache too
        if (!result.error && cachedBenchmarks) {
            const name = testPath.split('/').pop().replace('.bin', '');
            const cat = testPath.split('/')[1];
            const idx = cachedBenchmarks.findIndex(d => d.name === name);
            const entry = { name, category: cat, single: result.single, pipeline: result.pipeline };
            if (idx >= 0) cachedBenchmarks[idx] = entry;
            else cachedBenchmarks.push(entry);
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(result));
        return;
    }

    // Serve static files
    let filePath = url.pathname === '/' ? '/index.html' : url.pathname;
    const fullPath = path.join(__dirname, filePath);
    const ext = path.extname(fullPath);
    const mimeTypes = {
        '.html': 'text/html',
        '.js': 'application/javascript',
        '.css': 'text/css',
        '.json': 'application/json',
        '.png': 'image/png',
        '.svg': 'image/svg+xml'
    };

    if (fs.existsSync(fullPath) && fs.statSync(fullPath).isFile()) {
        res.writeHead(200, { 'Content-Type': mimeTypes[ext] || 'text/plain' });
        res.end(fs.readFileSync(fullPath));
    } else {
        res.writeHead(404);
        res.end('Not found');
    }
});

server.listen(PORT, () => {
    console.log(`RISC-V Pipeline Benchmark Dashboard: http://localhost:${PORT}`);
});
