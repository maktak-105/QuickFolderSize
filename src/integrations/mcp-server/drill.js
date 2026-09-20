const fs = require('fs');

const jsonFile = process.argv[2];
const targetPath = process.argv[3]; // e.g. "Microsoft" or "Microsoft/Edge"
const topN = parseInt(process.argv[4] || '25', 10);

const data = JSON.parse(fs.readFileSync(jsonFile, 'utf-8'));

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

let node = data.tree;
if (targetPath) {
  const parts = targetPath.split('/').filter(Boolean);
  for (const part of parts) {
    const next = (node.children || []).find((c) => c.name === part);
    if (!next) {
      console.log(`NOT FOUND: ${part} under ${node.name}`);
      process.exit(1);
    }
    node = next;
  }
}

console.log(`=== ${node.path} ===`);
console.log(`size: ${fmtSize(node.size)} (${node.size} bytes)  files: ${node.file_count}`);
console.log('');

const children = node.children || [];
const sorted = [...children].sort((a, b) => b.size - a.size);
console.log(`top ${topN} children by size:`);
for (const c of sorted.slice(0, topN)) {
  const type = c.is_dir ? 'dir' : 'file';
  console.log(`  ${fmtSize(c.size).padStart(12)}  files=${String(c.file_count).padStart(7)}  [${type}]  ${c.name}`);
}
