import { build } from 'esbuild'
import { readFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const raiz = join(dirname(fileURLToPath(import.meta.url)), '..')
// Vite permite `import x from './f.sql?raw'`; aquí se resuelve leyendo el archivo como texto.
const rawComoVite = {
  name: 'raw',
  setup(b) {
    b.onResolve({ filter: /\?raw$/ }, (a) => ({ path: resolve(a.resolveDir, a.path.replace(/\?raw$/, '')), namespace: 'raw' }))
    b.onLoad({ filter: /.*/, namespace: 'raw' }, (a) => ({ contents: 'module.exports = ' + JSON.stringify(readFileSync(a.path, 'utf8')), loader: 'js' }))
  }
}

await build({
  plugins: [rawComoVite],
  entryPoints: [join(raiz, 'src', 'headless.ts')],
  outfile: join(raiz, 'dist', 'main.cjs'),
  bundle: true,
  platform: 'node',
  target: 'node22',
  format: 'cjs',
  sourcemap: true,
  logLevel: 'info',
  // El módulo nativo no se empaqueta: se carga desde node_modules (precompilado para esta plataforma).
  external: ['better-sqlite3']
})
