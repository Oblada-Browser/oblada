// Renders background.html into background.png and background@2x.png (the DMG's window,
// on regular and Retina screens), headless with the Oblada build.
//   node branding/dmg/render.mjs [--mock <dir>]
// --mock also writes mock-light.png and mock-dark.png into <dir>: the window with the
// icons and Finder's labels, black in light mode and white in dark mode.
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { defaultApp, launch, until, watchFocus } from '../../chromium/scripts/lib/oblada.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const mockIndex = process.argv.indexOf('--mock');
const mockDir = mockIndex > 0 ? path.resolve(process.argv[mockIndex + 1]) : null;
// Where dmg_settings.py puts them.
const icons = {
  Oblada: [165, 220, path.join(defaultApp, 'Contents/Resources/app.icns')],
  Applications: [495, 220, '/System/Library/CoreServices/CoreTypes.bundle/Contents/Resources/ApplicationsFolderIcon.icns'],
};

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'oblada-dmg-'));
const focus = watchFocus();
const o = await launch({ profile: path.join(tmp, 'profile') });
try {
  const page = await o.open(`file://${here}/background.html`);
  await until('fonts', () => o.evaluate(
    `document.fonts.ready.then(() => document.fonts.check('50px Fraunces') && document.fonts.check('25px Caveat'))`, page), 30000);
  const shot = async (scale, file) => {
    await o.send('Emulation.setDeviceMetricsOverride',
                 { width: 660, height: 420, deviceScaleFactor: scale, mobile: false }, page);
    await new Promise((r) => setTimeout(r, 400));
    const { data } = await o.send('Page.captureScreenshot',
                                  { format: 'png', clip: { x: 0, y: 0, width: 660, height: 420, scale: 1 } }, page);
    fs.writeFileSync(file, Buffer.from(data, 'base64'));
    console.log(file);
  };
  await shot(1, path.join(here, 'background.png'));
  await shot(2, path.join(here, 'background@2x.png'));

  if (mockDir) {
    fs.mkdirSync(mockDir, { recursive: true });
    for (const [name, [x, y, icns]] of Object.entries(icons)) {
      const png = path.join(tmp, `${name}.png`);
      execFileSync('sips', ['-s', 'format', 'png', '--resampleWidth', '256', icns, '--out', png], { stdio: 'ignore' });
      await o.evaluate(`(() => {
        const img = Object.assign(document.createElement('img'), { src: 'file://${png}', className: 'icon' });
        img.style.left = '${x - 64}px'; img.style.top = '${y - 64}px';
        const label = Object.assign(document.createElement('div'), { className: 'label', textContent: '${name}' });
        label.style.left = '${x - 80}px'; label.style.top = '${y + 68}px';
        document.body.append(img, label);
        return img.decode();
      })()`, page);
    }
    for (const [mode, color] of [['light', '#000'], ['dark', '#fff']]) {
      await o.evaluate(`document.querySelectorAll('.label').forEach((l) => l.style.color = '${color}')`, page);
      await shot(2, path.join(mockDir, `mock-${mode}.png`));
    }
  }
} finally {
  await o.kill();
  fs.rmSync(tmp, { recursive: true, force: true });
  if (focus.stop().tookFocus) console.log('WARNING: Oblada took focus');
}
