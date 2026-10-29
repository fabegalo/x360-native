# CROSS-PLATFORM-FINAL — incrementos de infraestrutura (parcial)

Base `292ad19`; worktree `.local/worktrees/cross-platform-final`, branch
`codex/cross-platform-final`. Código em `a3499d3` e `5fce3f0`.
**O port completo solicitado não está concluído.**

## Implementação

- `x360n_process` é a mesma biblioteca usada pelo supervisor e pelo desktop.
  O backend POSIX usa `posix_spawn`, argumentos sem shell, cwd nativo, logs
  separados, grupo de processos privado, TERM com prazo de 250 ms e KILL/reap.
  Descendentes no grupo são encerrados também na saída normal do líder.
- Espera por pidfd no Linux e kqueue no Darwin, com polling limitado somente
  quando não há primitiva de saída disponível. Eventos e callbacks preservam
  timeout/cancelamento; falhas posteriores ao spawn não retornam sucesso.
- Logs POSIX recusam links simbólicos, aliases por hardlink e arquivos especiais,
  incluindo FIFO, antes de truncar. Descritores não destinados ao filho são fechados.
- Notificações de sessão mantêm eventos Windows e acrescentam FIFO POSIX por
  operação, em diretório privado, com validação de proprietário, permissões e links.
- Captura Windows usa os registradores e a máquina corretos em x64/ARM64.
  Relatórios preservam o sinal POSIX separadamente de `exit(128 + sinal)`.
- Caminhos usam caracteres nativos: Unicode POSIX não passa por `wstring`.
  Contenção respeita `/`, case e barras invertidas literais no POSIX; mantém a
  comparação Windows. Executáveis POSIX usam permissão de execução; continuam
  proibidos inputs e arquivos fora dos diretórios de build.
- Identidade do processo e localização do executável não dependem do cwd.

## Execuções realizadas

| Alvo | Evidência desta etapa | Limite |
|---|---|---|
| Windows x64 | Processos/IPC: 12 casos, 48 asserções; captura de falha: 1/14; caminhos novos: CTest PASS; regressões de caminhos: 5/45. Launcher/supervisor/extrator Release compilados | Nenhuma sessão original ou validação visual nova |
| Linux x64 / Ubuntu WSL | Binário ELF nativo: processos/IPC 20 casos, 110 asserções; caminhos novos 3/17; regressões de caminhos 5/45 | Build diagnóstico de contratos com GCC 15.2.0, não build do aplicativo/AOT com LLVM pinado |
| Windows ARM64 | Objetos de `child_process.cpp`, `paths.cpp` e fixture compilados com Clang 22.1.8, alvo aarch64 Windows | Cross-compilation; sem link completo ou execução nativa |
| Linux ARM64 | NOT RUN | Sem build/execução nesta etapa |
| macOS ARM64 | NOT RUN | Backend Darwin implementado, sem compilação/execução em Mac |
| macOS x64 | NOT RUN | Sem build/execução nesta etapa |

O caso mínimo de captura de stdout falhou contra `child_process.cpp` da base:
`r.launched == false`. O backend novo passou. Não houve suíte completa, jogo,
Computer Use, nova homologação C7/C8 ou medição de ganho de performance.

No WSL, GCC/CMake/Ninja e dependências foram baixados pelo APT e extraídos
somente na `.local` da worktree. Índices antigos/updates retornaram 404; as
versões base verificadas completaram o sysroot privado. Nenhum pacote foi
instalado globalmente. Isso não substitui LLVM 22.1.8 no pipeline de produção.

O primeiro configure Windows do desktop omitiu `-march=x86-64-v2` e falhou em
SSSE3 do SDK. Refeito com as flags do preset original, preservando a ISA.
Windows PowerShell manteve wrappers de build abertos após o Ninja encerrar;
foram interrompidos. A confirmação final executou o **mesmo** `ninja-guard.ps1`
no PowerShell 7, com as mesmas travas e target, e retornou **0**.

Logs e hashes locais: `.local/evidence/cross-platform-final/`; configurações e
builds em `.local/build/{platform-win,platform-linux,desktop-win}` da worktree.
`X360_PLATFORM_CONTRACTS_ONLY=ON` seleciona estes contratos sem runtime/GPU;
não pode ser combinado com desktop-only/distribuição. CTest permite filtrar
`platform.process`, `platform.paths` e `platform.path-regressions`.

## Pendências que impedem aceite completo

Launcher compartilhado, supervisor gráfico POSIX/transições, AOT único, extração
e criptografia portáveis, módulos ELF/Mach-O/recibos de identidade nativa,
LLVM pinado Linux/macOS, serviços/renderização, pacotes e CI das seis variantes
continuam pendentes. `session_window.cpp` ainda recusa janela persistente fora
do Windows; não há dashboard Linux/macOS demonstrada.

O grupo POSIX não garante eliminação após morte abrupta do supervisor nem de
descendentes que abandonem deliberadamente o grupo. Crash stacks/memória e
métricas de árvore POSIX são recusados explicitamente antes de lançar. Linux
exige glibc com `addclosefrom_np` (>=2.34); Darwin precisa das APIs de spawn/cwd
verificadas no configure. Esses limites não são certificados por cross-compilation.

SDK, lock, patches, originais, perfis/saves e recibos de títulos não foram
alterados. Sem push, tag, pacote novo ou Release. Não herdar PASS de contratos
para boot, imagem, gameplay ou fluxo completo em qualquer plataforma.
