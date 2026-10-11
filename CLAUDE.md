# CLAUDE.md — contexto persistente do x360-native

Leia também `STATUS.md` (estado real) e `docs/architecture.md` (decisões D1–D7).

## Objetivo

Plataforma Windows x64 sobre ReXGlue: jogos de Xbox 360 recompilados AOT para
código nativo e a **dashboard original** como alvo próprio (lançar jogo, salvar,
encerrar, voltar ao menu). "Todos os jogos" é meta de longo prazo, nunca uma
afirmação.

## Comandos (PowerShell, raiz do repositório)

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap.ps1   # submódulos, LLVM 22.1.8, symlinks libmspack
scripts\devshell.cmd cmake --preset win-amd64                    # configure (~7 min a frio)
scripts\devshell.cmd cmake --build --preset win-amd64-debug
scripts\devshell.cmd ctest --preset win-amd64-debug -j 16        # tudo (SDK + projeto)
scripts\devshell.cmd ctest --preset win-amd64-debug -L project   # só este repositório
.local\build\win-amd64\src\supervisor\Debug\x360n.exe doctor
```

- Sempre compilar via `scripts\devshell.cmd` (vcvars64 + LLVM fixado). Não há
  clang no PATH global, de propósito.
- `devshell.cmd` repassa os argumentos com as regras do cmd: `|` vira pipe e `^`
  some. Filtre por nome sem âncora (`-R "local[.]"`, `-R "x360n[.]ppc"`) ou pelo
  label único `project`.
- A mensagem `'vswhere.exe' não é reconhecido` vem de `VsDevCmd.bat:181` quando
  `NoDefaultCurrentDirectoryInExePath=1`; o próprio script diz que não afeta o ambiente.
- Se o build falhar, **não rode os testes** sobre binários antigos.

Resultado de referência: 1703 testes, 1697 ok, 5 skip, 1 falha conhecida
(`unit.Depfile escapes characters make treats as syntax`, bug do teste upstream).
Qualquer outra falha é regressão.

## Estrutura

- `external/rexglue-sdk` — submódulo fixado em `deps.lock.json`. **Não editar.**
  Exceção documentada: cópias dos symlinks do libmspack (`scripts/materialize-symlinks.ps1`).
- Correções do SDK vão no `CMakeLists.txt` raiz como backport condicionado ao
  commit fixado, citando o commit upstream equivalente.
- `src/supervisor` — `x360n` (doctor/identify/run). Não liga o runtime.
- `tests/` — prefixos: `x360n.*`, `x360n.ppc.*`, `sdkcheck.*`, `env.*`, `local.*`.
  Um único label CTest (`project`): listas de labels quebram `catch_discover_tests`.
- `titles/` — configuração por título/versão, indexada por SHA-256 do XEX.
- `.local/` — ignorado: toolchains, build, inputs (conteúdo do usuário), runs, logs.

## Regras

1. Não inventar: nomes de API, flags, targets, imports, endereços, hashes ou
   resultados. Confirmar no código da revisão fixada (`external/rexglue-sdk`);
   a wiki está desatualizada (ex.: `rexglue init` usa `--project-name/--xex-path`).
2. Marcar o que é fato, hipótese e não verificado. "Compilou" ≠ "inicia" ≠ "jogável".
   A matriz em `docs/compatibility.md` só avança com evidência (teste, hash, log).
3. Não esconder função ausente retornando sucesso/zero. Lembre: `REX_STUB` não
   define r3. Implementação parcial exige comportamento documentado e teste.
4. Não remover verificações nem afrouxar testes. Não usar `--force` no codegen sem
   registrar e sem marcar o resultado como não validado.
5. Para corrigir: reproduzir a falha → hipótese → mudança pequena → teste
   relevante → suíte completa.
6. Caminho principal AOT. Sem Xenia como executor, sem JIT/interpretador, sem
   autorização explícita. Xenia é referência.
7. Um processo de teste que abre e fecha valida o supervisor, nunca a dashboard.
   A interface de terminal não é a dashboard.
8. Conteúdo proprietário (XEX, dashboard, saves) só em `.local/inputs`, fornecido
   pelo usuário. Não baixar SDKs vazados nem conteúdo de terceiros. `x360n run`
   recusa executáveis em `.local/inputs`; não executar scripts vindos de dados de jogos.
9. Correções específicas de título ficam em `titles/`; correção geral precisa de
   teste que não dependa só daquele jogo.
10. Sem `install` do SDK, instalação global ou admin sem explicar e pedir
    (o `install` escreve em `HKCU\Software\Kitware\CMake\Packages`).
11. Fora do escopo atual: Xbox Live, multiplayer online, periféricos especiais.
12. Não afirmar desempenho sem medir e descrever a medição.
