// Executa um programa C compilado para WebAssembly/WASI dentro do Node.
// Uso: node ferramentas/executar-wasi.mjs programa.wasm [argumentos...]
//
// O diretório atual é exposto ao programa como "/", então caminhos relativos
// funcionam como num executável nativo. Útil onde executáveis nativos novos
// são bloqueados (ex.: Windows com Smart App Control) e para isolar a execução.
import { readFile } from 'node:fs/promises';
import { basename } from 'node:path';
import { WASI } from 'node:wasi';

const [arquivo, ...args] = process.argv.slice(2);
if (!arquivo) {
  console.error('uso: node executar-wasi.mjs programa.wasm [argumentos...]');
  process.exit(2);
}

const wasi = new WASI({
  version: 'preview1',
  args: [basename(arquivo, '.wasm'), ...args],
  env: { LANG: 'pt_BR.UTF-8' },
  preopens: { '/': process.cwd() },
  returnOnExit: true,
});
const modulo = await WebAssembly.compile(await readFile(arquivo));
const instancia = await WebAssembly.instantiate(modulo, wasi.getImportObject());
process.exitCode = wasi.start(instancia);
