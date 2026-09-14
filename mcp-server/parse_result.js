const fs = require('fs');

const inFile = process.argv[2];
const outJsonFile = process.argv[3];
const topN = parseInt(process.argv[4] || '30', 10);

const raw = fs.readFileSync(inFile, 'utf-8');

// SSE format: lines like "event: message" / "data: {...}"
let dataLine = null;
for (const line of raw.split('\n')) {
  if (line.startsWith('data:')) {
    dataLine = line.slice(5).trim();
    break;
  }
}
if (!dataLine) {
  // maybe plain JSON, not SSE
  dataLine = raw.trim();
}

const envelope = JSON.parse(dataLine);
const text = envelope.result.content[0].text;
const data = JSON.parse(text);

fs.writeFileSync(outJsonFile, JSON.stringify(data));

function fmtSize(bytes) {
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let v = bytes;
  let u = 0;
  while (v >= 1024 && u < units.length - 1) {
    v /= 1024;
    u++;
  }
  return `${v.toFixed(2)} ${units[u]}`;
}

console.log(`path: ${data.path}`);
console.log(`total_size: ${fmtSize(data.total_size)} (${data.total_size} bytes)`);
console.log(`subfolder_count: ${data.subfolder_count}`);
console.log(`file_count_recursive: ${data.file_count_recursive}`);
console.log('');

const children = (data.tree && data.tree.children) || [];
const sorted = [...children].sort((a, b) => b.size - a.size);

console.log(`top ${topN} immediate children by size:`);
for (const c of sorted.slice(0, topN)) {
  const type = c.is_dir ? 'dir' : 'file';
  console.log(`  ${fmtSize(c.size).padStart(12)}  files=${String(c.file_count).padStart(7)}  [${type}]  ${c.name}`);
}
