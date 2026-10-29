# CROSS-PLATFORM-FINAL — supervisor e pipeline AOT nativos

Base `292ad19`; worktree `.local/worktrees/cross-platform-final`, branch
`codex/cross-platform-final`. Código em `a3499d3` e `5fce3f0`.
O primeiro incremento abaixo foi integrado ao main. O segundo incremento
implementa o supervisor/pipeline compartilhados e os valida em Windows e Linux
x64; isso não certifica o aplicativo gráfico nas seis variantes.
Segundo incremento: `8625338`, integrado por fast-forward ao `main`. O launcher
Windows da pasta principal foi recompilado em Release (69 etapas, saída 0), com
supervisor atualizado em `.local/build/desktop-only/src/desktop/`.

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

## Segundo incremento: supervisor e preparação nativos

`x360n prepare <title>` executa o mesmo driver C++ nos hosts suportados. O script
PowerShell limita-se à seleção do supervisor, argumentos e ambiente MSVC;
`scripts/build-title.sh` encaminha os mesmos argumentos no POSIX. O desktop
Windows seleciona explicitamente o supervisor distribuído. Não há implementação
Linux paralela da lógica de preparação.

- Identidade de host/ABI, cabeçalhos PE/ELF/Mach-O e extensão dos módulos são
  verificados. Windows x64 preserva seus diretórios; os demais hosts usam
  `.local/build/<sistema-arquitetura>/...`, incluindo os imports compactos.
- Travas nativas de ambiente/título/build, marcadores de interrupção e publicação
  atômica protegem recibos e produtos. O Ninja usa uma cópia do supervisor como
  guard; um build direto que permanece integralmente autenticado preserva a
  preparação, enquanto mudanças/falhas exigem preparar novamente.
  O guard preserva o cwd dos sub-builds `try_compile` do CMake e recusa diretórios
  fora do seu build. Cada geração usa um snapshot estável do guard; relinkar o
  supervisor não força a recompilação do SDK genérico.
- O driver verifica o commit do SDK, os hashes dos patches e arquivos aplicados,
  a versão/target do Clang, fontes, ferramentas, ambiente e dependências reais
  do Ninja. Produtos têm fingerprints de conteúdo/comando/dependências; alterar
  fonte ou objeto preservando mtime não autoriza reutilização.
- SDK genérico usa gerações verificadas por host/toolchain e recusa unidades de
  tradução não identificadas ou pertencentes a títulos. Codegen tem produtor
  privado, manifests adicionais, saídas verificadas e recuperação após timeout.
  Os recibos existentes mantêm os anchors usados pelo verificador de lançamento.
- Originais continuam vinculados a seus SHA-256 e registros específicos. Chaves
  locais são encaminhadas apenas ao ambiente dos filhos e vinculadas por hash;
  seu conteúdo não é escrito nos logs/recibos.
- Módulos e preflight são exigidos conforme o registro; exports de ABI de GPU,
  módulos PPC e host de UI são conferidos antes da publicação. O estágio POSIX
  copia bibliotecas e preflight com resolução relativa de bibliotecas. D3D12
  permanece Windows; o padrão POSIX é Xenos/Vulkan, sem fallback silencioso.
- No POSIX, um guardião sem polling mantém o grupo de processos e encerra seus
  descendentes quando o supervisor morre, inclusive por SIGKILL. Ele fecha os
  descritores herdados para não manter vivas outras operações concorrentes.
  Overrides de ambiente são privados a cada filho nos dois backends.
- `X360_SUPERVISOR_ONLY=ON` compila supervisor/modelo/testes sem runtime/GPU.
  Sessões POSIX usam janelas nativas separadas dos títulos e preservam o loop
  compartilhado de handoff, armazenamento e preferências.

### Evidência do segundo incremento

| Execução | Resultado e limite |
|---|---|
| Windows x64 | Launcher, supervisor e testes Release compilados com Clang 22.1.8. Processos/caminhos: 20 casos, 112 asserções; contratos de launcher/sessão/preparação: 30/1135; modelo/importação AOT/renderizador: 13/276. Integração AOT PE/Unicode PASS com try_compile e reutilização do SDK após atualização do supervisor |
| Linux x64 / Ubuntu WSL | Supervisor/modelo compilados com Clang 22.1.8. Processos/caminhos: 30 casos, 182 asserções. Sessões: 6 casos, 956 asserções, 80 transições; último CTest PASS em 132,83 s |
| AOT Linux | Integração PASS: primeiro build, recibo aceito no lançamento, PE sob recibo legado recusado antes de execução, reutilização, guard sem alterações, try_compile, atualização do supervisor preservando o SDK, edição host sem codegen, mtime preservado, objeto adulterado, timeout/recuperação, ABI ausente/recuperação. SDK sintético neste teste: não traduz PPC |
| SDK/PPC Linux reais | Commit `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2` e 203 patches verificados; produtor `rexglue`, `librexruntime.so` e `librexgpu-xenos.so` compilados. Programa PPC original do repositório traduzido pelo produtor real, C++ gerado compilado e 5 testes PASS, incluindo FP estrito e aliasing |
| XEX real / Linux | Demo Iruka de SHA-256 `66ec34f2f558fe4e419d926108bf458a5c634ea8b5f76771b48ba633ecb61e31`, copiado para inputs isolados. Pipeline completo PASS até `iruka` ELF, bibliotecas, preflight e recibo publicados. Repetição retornou `reused verified preparation`, sem compilação/codegen |
| Demais sistemas/arquiteturas | Sem novas execuções nativas; os limites da tabela do primeiro incremento continuam válidos |

LLVM Linux: asset oficial `LLVM-22.1.8-Linux-X64.tar.xz`, SHA-256
`df0e1ecf16caf3489a272a5eea4eec9b0d82878f6477fa309504f918a0006384`.
Ferramentas/dependências Linux foram extraídas em sysroot privado (incluindo
X11/Wayland/pkg-config), sem instalação global. O cache de teste no disco ext4
do WSL evita o custo de metadados do NTFS; Git nativo é usado nos fixtures.
CMake Linux 4.2.3 e Ninja 1.13.2 são identificados por hash; Windows mantém suas
ferramentas existentes. Nenhuma troca de versão do ReXGlue ou do LLVM.

O SDK usado no preparo real é uma cópia privada no ext4 do WSL, com o mesmo
commit/arquivos dos 203 patches conferidos antes do build. O checkout Windows
permaneceu intacto. Com ferramentas e sources já disponíveis: preparo Iruka
15min29,56s, repetição 35,04s. São tempos locais com outputs em NTFS, sem comparação
controlada entre sistemas; não demonstram ganho de performance nem atingem a meta
histórica de primeiro preparo em menos de 10 minutos. A repetição verificou
conteúdo, sem aprovar cache apenas por timestamps.
Depois de atualizar o supervisor, o demo foi preparado novamente com o SDK e o
codegen reais reutilizados (6min00,10s, incluindo revalidação incremental). Essa
execução também terminou com recibo publicado. O Ninja executou a checagem do
codegen (um módulo já atualizado, zero saídas reescritas) e recompilou os objetos
do demo; a atualização do supervisor não ficou restrita a verificar hashes.
Essa limitação de desempenho está registrada, sem alegar ganho nesta passagem.

Evidências locais do segundo incremento, dentro da worktree:
`.local/linux-tools/{platform-final-test,native-session-final,native-pipeline-final,real-sdk-configure,real-sdk-build,real-ppc-staging-build,real-ppc-staging-test}.log`
e `.local/build/{platform-win,desktop-win}/` (`final-process-test.log`,
`native-pipeline-final.log`, `launcher-contracts-final.log`). Fixtures de AOT
contêm somente fontes próprias e não assinam produtos de jogos existentes.
O preparo do demo tem logs `iruka-native-{prepare,reuse}.{log,time}`. O diagnóstico
nativo foi executado com SDK/compilador/produtor explícitos: zero falhas; avisos
incluem os patches locais e a identificação upstream `dev.unknown`, preservados.

### Uso

```powershell
scripts/build-title.ps1 -Title <id> -Supervisor <caminho/x360n.exe>
```

```sh
export X360N_SUPERVISOR=/caminho/do/x360n
sh scripts/build-title.sh <id>
# Ou usar diretamente o driver, com ferramentas nativas explícitas:
x360n --root /caminho/do/projeto prepare <id> \
  --cc /caminho/clang --cxx /caminho/clang++ \
  --cmake /caminho/cmake --ninja /caminho/ninja
```

O SDK padrão é `external/rexglue-sdk`; `--sdk` permite uma cópia pinada explícita.
`--build-dir` permanece contido em `.local/build`; `--define NAME=VALUE`
encaminha configuração nativa, e `--timeout` limita cada comando. Não usar os
presets antigos de clang-20 para demonstrar esta validação com 22.1.8.
`doctor --sdk <sources> --cxx <clang++> --rexglue <produtor>` diagnostica uma
geração selecionada sem pressupor caminhos ou executáveis Windows no POSIX.

## Pendências que impedem aceite do port completo

Frontend gráfico compartilhado, extração/importação completa POSIX, serviços,
GPU/áudio/input reais, distribuição/CI e execução nas outras quatro variantes
continuam pendentes. `session_window.cpp` ainda não fornece uma janela persistente
POSIX; não há dashboard Linux/macOS visualmente demonstrada. O smoke PPC não
certifica um XEX comercial, boot, imagem, gameplay, storage original ou Vulkan
no hardware. Não houve nova homologação C7/C8 com jogo/dashboard.

O grupo POSIX não garante eliminação de descendentes que o abandonem
deliberadamente via `setsid`/`setpgid`. Crash stacks/memória e
métricas de árvore POSIX são recusados explicitamente antes de lançar. Linux
exige glibc com `addclosefrom_np` (>=2.34); Darwin precisa das APIs de spawn/cwd
verificadas no configure. Esses limites não são certificados por cross-compilation.

Fontes do SDK, lock, patches, originais, perfis/saves e recibos existentes da pasta
principal não foram alterados. O Iruka do teste tem inputs/build/recibo próprios
na worktree. Sem push, tag, pacote novo ou Release. Não herdar PASS de contratos
para boot, imagem, gameplay ou fluxo completo em qualquer plataforma.
