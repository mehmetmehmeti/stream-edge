const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const os = require('node:os');
const crypto = require('node:crypto');

async function main() {
  const root = process.cwd();
  const input = process.argv[2];
  if (!input) throw new Error('Usage, from server project root: node verify-lan-on-server.cjs expected-server-evidence.json');
  const expected = JSON.parse(fs.readFileSync(input));
  if (os.hostname() === expected.edge_host) throw new Error('Run this on the SERVER, not the edge laptop');
  const dotenv = require(path.join(root, 'backend-web/node_modules/dotenv'));
  const env = dotenv.parse(fs.readFileSync(path.join(root, 'backend-web/.env')));
  const db = new (require(path.join(root, 'backend-web/node_modules/pg')).Client)({ connectionString: env.DATABASE_URL });
  const report = { host: os.hostname(), captured_at: new Date().toISOString(), edge_host: expected.edge_host,
    files: [], commands: [], streams: [], server_browser_playback_confirmed: null };
  const recordingRoot = fs.realpathSync(env.CLOUD_RECORDINGS_DIR || path.join(root, 'backend-web/recordings'));
  await db.connect();
  try {
    report.database_version = (await db.query('SELECT version() AS version')).rows[0].version;
    for (const item of expected.videos) {
      const video = (await db.query('SELECT id,stream_id,status,cloud_path,frame_count::text,size_bytes::text,version FROM videos WHERE id=$1', [item.id])).rows[0];
      if (!video || video.status !== 'COMPLETED' || video.stream_id !== item.stream_id) throw new Error('Video metadata/status mismatch: ' + item.id);
      const file = fs.realpathSync(video.cloud_path);
      if (!file.startsWith(recordingRoot + path.sep)) throw new Error('File outside configured server recordings directory');
      const hash = crypto.createHash('sha256');
      for await (const chunk of fs.createReadStream(file)) hash.update(chunk);
      const sha256 = hash.digest('hex');
      const probe = JSON.parse(cp.execFileSync('ffprobe', ['-v','error','-count_frames','-select_streams','v:0',
        '-show_entries','stream=codec_name,width,height,nb_read_frames,avg_frame_rate:format=duration,size,bit_rate','-of','json',file], {encoding:'utf8',timeout:120000}));
      if (sha256 !== item.sha256 || String(fs.statSync(file).size) !== String(item.size_bytes) ||
          String(probe.streams[0].nb_read_frames) !== String(item.frame_count) || video.frame_count !== String(item.frame_count))
        throw new Error('Actual server file verification failed: ' + item.id);
      report.files.push({id:item.id,path:file,sha256,size_bytes:fs.statSync(file).size,probe,metadata:video});
    }
    for (const item of expected.commands) {
      const command=(await db.query('SELECT id,status,stream_id,video_id,version FROM commands WHERE id=$1',[item.id])).rows[0];
      if(!command||command.status!=='COMPLETED'||command.version!==item.version)throw new Error('Command mismatch: '+item.id);
      report.commands.push(command);
    }
    for (const id of expected.stream_ids) {
      const stream=(await db.query('SELECT id,status,mediasoup_stream_id,version FROM streams WHERE id=$1',[id])).rows[0];
      if(!stream||stream.status!=='ENDED')throw new Error('Stream not ended: '+id);
      report.streams.push(stream);
    }
    report.sync_counts={};
    for(const table of ['sync_inbox','sync_outbox'])report.sync_counts[table]=(await db.query(`SELECT status,count(*)::text AS count FROM ${table} GROUP BY status`)).rows;
    report.old_oct15_counts={};
    for(const table of ['streams','videos','commands'])report.old_oct15_counts[table]=(await db.query(`SELECT count(*)::int AS count FROM ${table} WHERE created_at>='2026-10-15 00:00:00' AND created_at<'2026-10-16 00:00:00'`)).rows[0].count;
    report.interfaces=os.networkInterfaces();report.os=fs.readFileSync('/etc/os-release','utf8');report.node=process.version;
    report.listeners=cp.execFileSync('ss',['-ltnp'],{encoding:'utf8'});
    report.media_udp=cp.execFileSync('ss',['-lunp','sport = :40000'],{encoding:'utf8'});
    report.status='passed';
  } finally { await db.end(); }
  const out=path.join(root,'deployment/private/server-lan-verification.json');
  fs.mkdirSync(path.dirname(out),{recursive:true,mode:0o700});
  fs.writeFileSync(out,JSON.stringify(report,null,2)+'\n',{mode:0o600});
  console.log(JSON.stringify({status:report.status,host:report.host,files:report.files.length,commands:report.commands.length,output:out}));
}
main().catch(e=>{console.error(e.message);process.exitCode=1;});
