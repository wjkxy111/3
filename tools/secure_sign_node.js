/* Small host-side helper. Private keys never belong in firmware. */
const fs = require('fs');
const crypto = require('crypto');

const [command, keyPath, hex] = process.argv.slice(2);
if (command === 'generate') {
  const pair = crypto.generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  fs.writeFileSync(keyPath, pair.privateKey.export({ type: 'pkcs8', format: 'pem' }));
  fs.writeFileSync(`${keyPath}.pub.pem`, pair.publicKey.export({ type: 'spki', format: 'pem' }));
  process.stdout.write('generated ECDSA P-256 key pair\n');
  process.exit(0);
}
if ((command !== 'sign' && command !== 'verify') || !keyPath || !hex) {
  process.stderr.write('usage: sign|verify key.pem canonical_hex\n');
  process.exit(2);
}
const key = fs.readFileSync(keyPath);
const message = Buffer.from(hex, 'hex');
if (command === 'sign') {
  const signature = crypto.createSign('sha256')
    .update(message)
    .sign({ key, dsaEncoding: 'ieee-p1363' });
  process.stdout.write(signature.toString('hex'));
} else {
  const signature = Buffer.from(process.env.AUTOSAR_SIGNATURE_HEX || '', 'hex');
  const ok = crypto.createVerify('sha256')
    .update(message)
    .verify({ key, dsaEncoding: 'ieee-p1363' }, signature);
  process.stdout.write(ok ? 'OK' : 'FAIL');
  process.exit(ok ? 0 : 1);
}
