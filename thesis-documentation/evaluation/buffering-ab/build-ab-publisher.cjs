const fs=require('node:fs');
const path=require('node:path');
const cp=require('node:child_process');
const crypto=require('node:crypto');
const directory=__dirname;
const root=process.argv[2]||'<PROJECT_ROOT>';
const output=process.argv[3]||path.join(directory,'ab-build');
fs.mkdirSync(output,{recursive:true});
const source=path.join(root,'media-server/.tools/simple-whip-client/src/whip-client.c');
const packages=['gstreamer-webrtc-1.0','gstreamer-sdp-1.0','gstreamer-video-1.0','libsoup-3.0','json-glib-1.0'];
const flags=cp.execFileSync('pkg-config',['--cflags','--libs',...packages],{encoding:'utf8'}).trim().split(/\s+/);
const common=['-O2','-g','-Wall','-Wextra','-D_GNU_SOURCE','-include',path.join(directory,'ab-probe.h')];
for(const [name,file]of [['ab-whip-client',source],['ab-probe-smoke',path.join(directory,'ab-probe-smoke.c')]]){
  cp.execFileSync('gcc',[...common,file,path.join(directory,'ab-probe.c'),'-o',path.join(output,name),...flags],{stdio:'inherit'});
}
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
fs.writeFileSync(path.join(output,'build-provenance.json'),JSON.stringify({upstream_source:source,upstream_sha256:hash(source),probe_sha256:hash(path.join(directory,'ab-probe.c')),header_sha256:hash(path.join(directory,'ab-probe.h')),binary_sha256:hash(path.join(output,'ab-whip-client')),compiler:cp.execFileSync('gcc',['--version'],{encoding:'utf8'}).split('\n')[0],pkg_versions:cp.execFileSync('pkg-config',['--modversion',...packages],{encoding:'utf8'}).trim().split('\n')},null,2)+'\n');
console.log('Built instrumented WHIP client and synthetic probe check in '+output);
