// Servidor da captura de movimento: entrega a página da câmera e repassa cada quadro para o jogo.
//   http://localhost:47810   -> câmera do PC (localhost já é "seguro" para o navegador liberar a câmera)
//   https://<ip-da-rede>:47843 -> celular (câmera e giroscópio só funcionam em HTTPS; certificado próprio)
// Os quadros chegam por POST /f (texto) e vão por UDP para 127.0.0.1:47811, onde o jogo escuta.
'use strict';
const http = require('http');
const https = require('https');
const fs = require('fs');
const path = require('path');
const dgram = require('dgram');
const { execFileSync } = require('child_process');

const HTTP_PORT = 47810, HTTPS_PORT = 47843, GAME_PORT = 47811;
const dir = __dirname;
const certDir = path.join(dir, 'cert');
const keyFile = path.join(certDir, 'key.pem'), certFile = path.join(certDir, 'cert.pem');

if (!fs.existsSync(keyFile) || !fs.existsSync(certFile)) {
  fs.mkdirSync(certDir, { recursive: true });
  execFileSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '3650', '-subj', '/CN=GA mocap',
    '-keyout', keyFile, '-out', certFile], { stdio: 'ignore' });
}

const udp = dgram.createSocket('udp4');
const page = () => fs.readFileSync(path.join(dir, 'index.html'));
let lastFrom = '', frames = 0;

function handler(req, res) {
  if (req.method === 'POST' && req.url.startsWith('/f')) {
    let body = '';
    req.on('data', (c) => { body += c; if (body.length > 20000) req.destroy(); });
    req.on('end', () => {
      udp.send(Buffer.from(body), GAME_PORT, '127.0.0.1');
      frames++;
      lastFrom = req.socket.remoteAddress;
      res.writeHead(204, { 'Access-Control-Allow-Origin': '*' });
      res.end();
    });
    return;
  }
  if (req.url === '/' || req.url.startsWith('/?')) {
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
    res.end(page());
    return;
  }
  res.writeHead(404);
  res.end();
}

http.createServer(handler).listen(HTTP_PORT, '127.0.0.1');
https.createServer({ key: fs.readFileSync(keyFile), cert: fs.readFileSync(certFile) }, handler).listen(HTTPS_PORT, '0.0.0.0');
console.log(`[mocap] pronto: http://localhost:${HTTP_PORT}  https://<ip>:${HTTPS_PORT}`);
setInterval(() => { if (frames) console.log(`[mocap] ${frames} quadros/5s de ${lastFrom}`); frames = 0; }, 5000);
