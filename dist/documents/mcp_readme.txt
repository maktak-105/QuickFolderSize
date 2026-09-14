QuickFolderSize - MCP Server
Optional developer tool (bundled in the mcp-server\ folder of this package)

What it does
------------
A Node.js sidecar that exposes QuickFolderSize_cli.exe as an HTTP MCP
(Model Context Protocol) tool. It lets AI agents such as Claude Code
call folder scans directly, without opening the GUI.

It runs as a separate process from the main app (QuickFolderSize.exe).
Unlike the GUI and CLI, it is not a self-contained executable - it
ships as Node.js source. See the source repo for updates:
  https://github.com/maktak-105/QuickFolderSize

How to start
------------
1. Install Node.js (18 or later recommended): https://nodejs.org/
2. Install dependencies once, from the mcp-server folder:
     cd mcp-server
     npm install
3. Start it:
     start-admin.bat
     (or start-admin.ps1)

Must run elevated. Without administrator rights the CLI can't use the
NTFS MFT fast path and falls back to the slow Win32 walk, which puts
real load on the system when scanning large folders.

Once started, it listens on http://127.0.0.1:39391/mcp (port
configurable via the QFS_MCP_PORT environment variable). Closing the
elevated PowerShell window stops the server.

Tools exposed
-------------
server_status
  Returns whether this server process is running elevated. Use it
  as a connectivity check.

scan_folder(path, pretty?)
  Recursively scans path and returns the full JSON in a single
  response. Good for small-to-medium folders.

start_scan(path, pretty?)
  Starts a scan in the background and returns a scanId immediately.
  Use it for large or slow folders.

get_scan_result(scanId)
  Polls a start_scan job. On completion, returns a lightweight
  summary (top-level child folder sizes, etc.) plus the full tree's
  location on disk.

Operational notes (important)
------------------------------
Tool calls from the Claude Code side have an undocumented wait
timeout that is much shorter than the server's own 5-minute timeout.
Scanning a large folder through scan_folder can take long enough that
the caller reports a generic "MCP server \"quickfoldersize\" session
expired" error, even though the server answered normally. Small
folders (a few hundred files) succeed without issue.

There are two workarounds.

1. Use the async start_scan / get_scan_result pattern (above). It
   keeps each individual tool call short.

2. Curl the HTTP endpoint directly from a Bash tool call. This
   sidesteps Claude Code's own tool-call timeout entirely.

   Example:
     node -e "require('fs').writeFileSync('req.json', JSON.stringify({
       jsonrpc:'2.0', id:1, method:'tools/call',
       params:{name:'scan_folder',
               arguments:{path:'C:/Users/me/AppData/Local', pretty:false}}
     }))"
     curl -s -X POST http://127.0.0.1:39391/mcp ^
       -H "Content-Type: application/json" ^
       -H "Accept: application/json, text/event-stream" ^
       --data-binary @req.json

   The response is SSE-formatted (a "data: {...}" line carries the
   JSON-RPC result). In Node.js, JSON.parse the data line, then
   JSON.parse result.content[0].text again to get the scan result.

   Write Windows paths in the request using "/" (forward slash).
   A "\\" (double backslash) can get mangled by intermediate
   escaping in tool calls.

3. A large folder's response can run to tens of millions of
   characters (over 100 million for all of AppData\Local). Don't
   read the full tree directly - pipe it through a small script that
   only prints top-level child folder sizes.

Bundled scripts
----------------
parse_result.js <raw curl output.txt> <output.json> [topN]
  Parses the SSE response from a direct curl call, saves the full
  tree to a JSON file, and prints a breakdown of top-level child
  folders to stdout.

drill.js <tree json> <slash-separated subpath> [topN]
  Re-fetches the breakdown of any subfolder from a previously saved
  full tree JSON, with no rescan needed.

Reflecting code changes
------------------------
Editing server.js does not affect the running process. In the
elevated PowerShell window, press Ctrl+C and re-run start-admin.bat.

Also, Claude Code fetches and caches the MCP server's tool list once,
at the start of a session. Restarting the server after adding or
changing tools does not make the new tools visible within the same
conversation - a new conversation is required. Existing tools such as
server_status keep working within the same conversation via
reconnection.

More documentation
-------------------
See also, in the source repository:
- mcp-server/README.md   (the detailed original this file is based on)
- README.md               "MCP Server" section
- document/spec.md         Section 10 (technical spec)
- HISTORY.md               v3.0.0 (why this tool was added)
