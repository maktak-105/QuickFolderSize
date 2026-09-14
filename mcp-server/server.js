const express = require('express');
const { z } = require('zod');
const { McpServer } = require('@modelcontextprotocol/sdk/server/mcp.js');
const { StreamableHTTPServerTransport } = require('@modelcontextprotocol/sdk/server/streamableHttp.js');
const { spawn } = require('node:child_process');
const path = require('node:path');
const fs = require('node:fs');
const crypto = require('node:crypto');

const PORT = process.env.QFS_MCP_PORT || 39391;
const CLI_PATH = path.join(__dirname, '..', 'dist', 'binary', 'QuickFolderSize_cli.exe');
const SCAN_TIMEOUT_MS = 5 * 60 * 1000;
const REPORTS_DIR = path.join(__dirname, 'scan-reports');
if (!fs.existsSync(REPORTS_DIR)) fs.mkdirSync(REPORTS_DIR, { recursive: true });

// Long scans can exceed the MCP client's per-call timeout even though the
// server itself (SCAN_TIMEOUT_MS = 5min) is still happily waiting on the CLI.
// start_scan/get_scan_result decouple the CLI's real runtime from any single
// tool-call round trip: start_scan returns immediately with a job id, and the
// caller polls get_scan_result until done. The full tree is written to disk
// (scan-reports/<id>.json) instead of being returned inline, since it can be
// tens of MB for large trees; only a lightweight top-level summary comes back.
const scans = new Map();

function summarize(json) {
  const data = JSON.parse(json);
  const children = (data.tree && data.tree.children) || [];
  const topChildren = [...children]
    .sort((a, b) => b.size - a.size)
    .map((c) => ({ name: c.name, size: c.size, file_count: c.file_count, is_dir: c.is_dir }));
  return {
    path: data.path,
    scanned_at: data.scanned_at,
    total_size: data.total_size,
    subfolder_count: data.subfolder_count,
    file_count_recursive: data.file_count_recursive,
    top_level_children: topChildren,
  };
}

function startScanAsync(targetPath, pretty) {
  const scanId = crypto.randomUUID();
  const entry = { status: 'running', targetPath, startedAt: Date.now() };
  scans.set(scanId, entry);
  runScan(targetPath, pretty)
    .then((json) => {
      const reportFile = path.join(REPORTS_DIR, `${scanId}.json`);
      fs.writeFileSync(reportFile, json, 'utf-8');
      entry.status = 'done';
      entry.finishedAt = Date.now();
      entry.reportFile = reportFile;
      entry.summary = summarize(json);
    })
    .catch((err) => {
      entry.status = 'error';
      entry.finishedAt = Date.now();
      entry.error = err.message;
    });
  return scanId;
}

function isElevated() {
  try {
    fs.accessSync('C:\\Windows\\System32\\config\\SAM', fs.constants.R_OK);
    return true;
  } catch {
    return false;
  }
}

function runScan(targetPath, pretty) {
  return new Promise((resolve, reject) => {
    if (!fs.existsSync(CLI_PATH)) {
      reject(new Error(`CLI not found: ${CLI_PATH}`));
      return;
    }
    const args = [targetPath];
    if (pretty) args.push('--pretty');

    const child = spawn(CLI_PATH, args, { windowsHide: true });
    let stdout = '';
    let stderr = '';
    const timer = setTimeout(() => {
      child.kill();
      reject(new Error(`scan timed out after ${SCAN_TIMEOUT_MS}ms: ${targetPath}`));
    }, SCAN_TIMEOUT_MS);

    child.stdout.on('data', (d) => { stdout += d; });
    child.stderr.on('data', (d) => { stderr += d; });
    child.on('error', (err) => {
      clearTimeout(timer);
      reject(err);
    });
    child.on('close', (code) => {
      clearTimeout(timer);
      if (code !== 0) {
        reject(new Error(`CLI exited with code ${code}: ${stderr || stdout}`));
        return;
      }
      resolve(stdout);
    });
  });
}

function buildServer() {
  const server = new McpServer({ name: 'quickfoldersize-mcp', version: '1.0.0' });

  server.registerTool(
    'scan_folder',
    {
      title: 'Scan folder size (MFT-accelerated when elevated)',
      description:
        'Recursively scans a folder and returns size/file-count breakdown as JSON. ' +
        'Runs QuickFolderSize_cli.exe. Uses NTFS MFT fast-path automatically when this server process has administrator privileges; otherwise falls back to a slower Win32 walk.',
      inputSchema: {
        path: z.string().describe('Absolute path of the folder to scan, e.g. C:\\Users\\me\\AppData\\Local'),
        pretty: z.boolean().optional().describe('Pretty-print the JSON output (default: false, compact)'),
      },
    },
    async ({ path: targetPath, pretty }) => {
      try {
        const json = await runScan(targetPath, !!pretty);
        return { content: [{ type: 'text', text: json }] };
      } catch (err) {
        return {
          content: [{ type: 'text', text: `Error: ${err.message}` }],
          isError: true,
        };
      }
    }
  );

  server.registerTool(
    'start_scan',
    {
      title: 'Start an async folder scan (returns immediately)',
      description:
        'Kicks off a recursive scan in the background and returns a scanId right away, without waiting for the CLI to finish. ' +
        'Use this instead of scan_folder for large/slow folders (many files, deep trees) where scan_folder times out client-side. ' +
        'Poll get_scan_result with the returned scanId until status is "done" or "error".',
      inputSchema: {
        path: z.string().describe('Absolute path of the folder to scan'),
        pretty: z.boolean().optional().describe('Pretty-print the JSON file written to disk (default: false)'),
      },
    },
    async ({ path: targetPath, pretty }) => {
      const scanId = startScanAsync(targetPath, !!pretty);
      return { content: [{ type: 'text', text: JSON.stringify({ scanId, status: 'running' }) }] };
    }
  );

  server.registerTool(
    'get_scan_result',
    {
      title: 'Poll an async scan started with start_scan',
      description:
        'Returns the status of a scan started via start_scan. While running: {status:"running"}. ' +
        'When done: {status:"done", summary:{path,total_size,subfolder_count,file_count_recursive,top_level_children}, reportFile} ' +
        'where reportFile is the absolute path of the full recursive JSON tree written to disk (read it directly for deep drill-down). ' +
        'On failure: {status:"error", error}.',
      inputSchema: {
        scanId: z.string().describe('The scanId returned by start_scan'),
      },
    },
    async ({ scanId }) => {
      const entry = scans.get(scanId);
      if (!entry) {
        return { content: [{ type: 'text', text: JSON.stringify({ status: 'error', error: `unknown scanId: ${scanId}` }) }], isError: true };
      }
      if (entry.status === 'running') {
        return { content: [{ type: 'text', text: JSON.stringify({ status: 'running', targetPath: entry.targetPath, elapsedMs: Date.now() - entry.startedAt }) }] };
      }
      if (entry.status === 'error') {
        return { content: [{ type: 'text', text: JSON.stringify({ status: 'error', error: entry.error }) }], isError: true };
      }
      return {
        content: [
          {
            type: 'text',
            text: JSON.stringify({ status: 'done', summary: entry.summary, reportFile: entry.reportFile }),
          },
        ],
      };
    }
  );

  server.registerTool(
    'server_status',
    {
      title: 'Report MCP server privilege status',
      description: 'Returns whether this MCP server process is running with administrator privileges.',
      inputSchema: {},
    },
    async () => {
      const elevated = isElevated();
      return {
        content: [
          {
            type: 'text',
            text: JSON.stringify({ elevated, cliPath: CLI_PATH, cliExists: fs.existsSync(CLI_PATH) }),
          },
        ],
      };
    }
  );

  return server;
}

const app = express();
app.use(express.json());

app.post('/mcp', async (req, res) => {
  try {
    const server = buildServer();
    const transport = new StreamableHTTPServerTransport({ sessionIdGenerator: undefined });
    res.on('close', () => {
      transport.close();
      server.close();
    });
    await server.connect(transport);
    await transport.handleRequest(req, res, req.body);
  } catch (err) {
    console.error('Error handling MCP request:', err);
    if (!res.headersSent) {
      res.status(500).json({
        jsonrpc: '2.0',
        error: { code: -32603, message: 'Internal server error' },
        id: null,
      });
    }
  }
});

app.get('/mcp', (req, res) => {
  res.status(405).json({
    jsonrpc: '2.0',
    error: { code: -32000, message: 'Method not allowed (stateless server: use POST)' },
    id: null,
  });
});

app.delete('/mcp', (req, res) => {
  res.status(405).json({
    jsonrpc: '2.0',
    error: { code: -32000, message: 'Method not allowed (stateless server: use POST)' },
    id: null,
  });
});

app.listen(PORT, '127.0.0.1', () => {
  console.log(`QuickFolderSize MCP server listening on http://127.0.0.1:${PORT}/mcp`);
  console.log(`Elevated (administrator): ${isElevated()}`);
  console.log(`CLI: ${CLI_PATH} (exists: ${fs.existsSync(CLI_PATH)})`);
});
