# LAUNCHER-UNIFIED-01 — launcher gráfico único, 29/10/2026

**Resultado:** o launcher Win32/GDI foi substituído por **uma** implementação
C++ com SDL3 + Dear ImGui, construída pelo mesmo target `x360-native` a partir
de um único estado/controller, uma única árvore de páginas e um único conjunto
de textos. No Windows x64 ele compila, passa os testes focados e abre de verdade
com SDL_GPU/Direct3D 12 e com o rasterizador de software. No Linux x64 (WSL2)
o mesmo código compila, passa os mesmos testes do launcher e abre com
SDL_GPU/Vulkan e com o rasterizador de software. O frontend Win32 saiu do build
de produção e do repositório. macOS e ARM64 não foram compilados nem executados.

## O que mudou

| Antes | Agora |
|---|---|
| `src/desktop/win32/main_win32.cpp` (2.875 linhas: estado, regras, workers, layout e pintura GDI juntos) | `app/` (estado, controller, operações, serviços do sistema) + `ui/` (frontend, componentes, 6 páginas, apresentação) |
| GDI/GDI+, Segoe UI, Segoe MDL2 Assets, `MessageBox`, `IFileOpenDialog`, XInput, `WM_DROPFILES` | SDL3 (janela, DPI, eventos, controle, diálogo nativo de arquivo/pasta, arrastar e soltar), Dear ImGui, fontes Apache-2.0 embutidas, ícones vetoriais próprios, diálogos modais no tema |
| Preparação por `prepare-tools.ps1` + `build-title.ps1` | Ferramentas: `prepare-tools.ps1` (Windows) ou PATH (Linux/macOS); preparação: `x360n prepare <título> --keep-incomplete` direto (Windows dentro de `scripts/devshell.cmd`, que só fixa MSVC/LLVM) |
| 5 etapas inferidas (Ferramentas, Configuração, Tradução, Compilação, Registro) | 7 etapas reais: Ferramentas, SDK, Configuração, Codegen, Compilação, Verificação (marcadores `X360N_STAGE` do supervisor) e Registro; reutilizadas contam como concluídas; nenhum percentual |
| Verificação de ambiente só D3D12/MSVC | `CheckEnvironment` por sistema: CPU, memória, D3D12 (Windows), sonda Vulkan carregada sob demanda (todos), espaço, permissão, toolchain (MSVC+Windows SDK no Windows; clang/clang++/cmake/ninja/git no PATH em Linux/macOS) |
| Atualização só `windows-x64` | `PackagePlatform()` (`windows-x64`, `linux-x64`, `macos-arm64`, …); a seleção recusa pacotes de outro sistema/arquitetura |

Preservado sem alteração: catálogo, importação, biblioteca, recibos, SHA-256,
cancelamento, retomada, supervisor, sessão, notificação de fechamento,
atualização transacional (`ApplyUpdate`/`ConfirmUpdates`) e o formato de
`data/preferences.txt`. Nenhum dado é migrado, apagado ou invalidado.

APIs gráficas por sistema: Windows mantém D3D12 padrão e Vulkan atrás do
acesso de desenvolvimento (regra do supervisor); Linux/macOS mostram apenas
Vulkan (padrão), e Native fica desabilitado com o motivo (exige D3D12). Uma
escolha incompatível é recusada, nunca trocada em silêncio. Sair do acesso de
desenvolvimento volta ao renderer/API padrão do sistema.

## Evidências (Windows 11 x64, LLVM 22.1.8, build desktop-only Release)

| Verificação | Resultado |
|---|---|
| `x360-native` (desktop-only, sem runtime de títulos) | Compilado |
| `x360n_launcher_tests` — controller: sessões consecutivas, cancelamento antes da sessão, conteúdo alterado com mesmo timestamp, verificação seletiva, inventário de 5.001 arquivos sem hash, fechamento nativo antes da limpeza, recibos, remoção recusada por padrão, seletor cancelado, APIs por sistema, etapas e comando de preparação | 16 casos, 179 asserções, aprovado; listagem completa 883 ms; sessão: filho em 65–85 ms, ocioso 15–16 ms após sair |
| `x360n_launcher_tests "[launcher-layout]"` — todas as páginas × banners (ocioso, preparando, erro, importado) em 1000×700 e 1220×800 a 100/150 %; Configurações também em 1280×720, 1600×900, 1920×1080 e 125/200 %; PT-BR e EN; rolada até o fim | 208.685 asserções, aprovado; 304 PNGs pelo rasterizador do launcher, inspecionados por amostragem |
| Navegação por comando entre todas as páginas a partir de ambiente vazio | Aprovado, sem erro nem worker extra |
| `x360n_desktop_tests` (`~[update-crash]`, inclui atualização por plataforma) | 116 casos, 54.465 asserções, aprovado |
| Smoke real `x360-native.exe --smoke` (janela, 60 quadros, ambiente vazio) | `SDL_GPU direct3d12`, 1220×800, escala 1, Início/“Biblioteca vazia”, sem erro; `--renderer software`: mesmo estado |

Medições (10 s após 4 s de abertura, ambiente vazio): CPU ociosa 0,00 % nos
três casos; working set 26,7 MB (Win32, build de 27/10) → 94,2 MB (SDL_GPU) ou
48,4 MB (software); memória privada 5,1 → 112,4 / 28,7 MB; handles 305 → 546 /
398. O aumento vem do dispositivo D3D12 do SDL_GPU. Animação durante
operação e tempo de quadro no Linux não foram medidos.

## Linux x64 (Ubuntu WSL2, WSLg)

Mesmo target, desktop-only Release, LLVM 22.1.8 e o sysroot privado de
validação (`~/.cache/x360n-native-validation` no ext4 do WSL: CMake, Ninja,
pkg-config, X11/Wayland), sem pacote global. SDL3 configurado com vídeo
`x11(dynamic)` e `wayland(dynamic)` e GPU `vulkan`.

| Verificação | Resultado |
|---|---|
| `x360-native` ELF x86-64, `x360n_launcher_tests`, `x360n_desktop_tests` | Compilados |
| Controller (`[launcher-ui]`, opções, recibos, biblioteca) | 15 casos, 165 asserções, aprovado; listagem completa 811 ms |
| Layout headless (`[launcher-layout]`) | 3 casos, 203.968 asserções, aprovado (Linux oferece só a API Vulkan, por isso menos controles) |
| Smoke real no WSLg, ambiente vazio | `SDL_GPU vulkan`: 1220×800, Início/“Biblioteca vazia”, sem erro, 1,1 s; `software`: mesmo estado, 5,1 s para 60 quadros |
| `x360n_desktop_tests ~[update-crash]` | 58/75 casos; as 17 falhas são testes pré-existentes do modelo, nunca rodados no Linux, que (a) usam artefatos `build/titles/<id>/<id>.exe` em texto, recusados pela validação de imagem nativa do POSIX, ou (b) injetam falha por bloqueio de arquivo do Windows. O teste de atualização por plataforma deste incremento passa |

Correções que o Linux exigiu: `app/strings.h` foi renomeado para
`app/app_text.h`, porque o `<string.h>` da glibc inclui `<strings.h>` e
resolvia para o arquivo do projeto; o fixture do launcher passou a usar o
caminho nativo do título (`NativeTitleExecutable`) e uma imagem nativa real
(o próprio processo-fixture, nunca executado como título); o fixture de sessão
sinaliza o fechamento por `SessionNotification`, portável.

O dispositivo Vulkan usado pelo WSLg nesta execução não foi identificado no
smoke; na validação anterior do mesmo ambiente era llvmpipe (CPU). GPU física,
controle, diálogos de arquivo (portal/zenity) e arrastar e soltar não foram
exercitados no Linux.

## Fluxo real pelo launcher (Windows, autorizado em 29/10)

Ambiente de desenvolvimento do repositório, dashboard Retail 17559, saves e
perfis da sessão redirecionados para uma pasta descartável em `.local/runs`
(`--user-data-root` do supervisor); dados reais do mantenedor intocados. O
launcher foi conduzido pelo auxílio de revisão `--auto-start` (somente fora da
distribuição), que emite os mesmos comandos dos botões e registra o que a
interface mostra; nenhuma entrada foi injetada em janelas.

1. A listagem marcou a 17559 como candidata (“Conferir e iniciar”).
2. Iniciar foi recusado pela conferência antes da sessão: o recibo citava
   `cmake/build/x360n_build_isolation.cmake`, alterado depois da preparação de
   28/10. Mensagem: “A preparação mudou. Selecione Preparar…”.
3. **Defeito encontrado:** a ação principal virava “Adicionar conteúdo”, porque
   uma falha de início marcava os arquivos como inválidos mesmo quando o worker
   acabara de verificá-los (regra herdada do Win32). Corrigido: um início
   recusado depois da verificação adota o estado verificado; com arquivos
   íntegros a ação é “Verificar e preparar”. Teste novo: “a launch refused for a
   changed preparation offers to prepare again”.
4. Com a correção, a preparação começou pelo launcher (downloads recusados;
   ferramentas reutilizadas do cache) e mostrou as etapas reais do supervisor:
   `sdk-configure`, `sdk-build` (geração compartilhada do SDK reconstruída) e
   `1-configure`. Essas três não estavam mapeadas nas etapas da barra; agora
   estão (SDK, SDK e Configuração), com teste.
5. **A primeira preparação falhou em 8 min 53 s** no configure do título: o plugin
   Native (`src/native/CMakeLists.txt`) vincula `xxHash::xxhash`, mas a
   geração compartilhada do SDK exporta os alvos com o nome real (`xxhash`) e
   o consumidor só recriava os aliases `rex::*`. O plugin entra porque o cache
   do build do título guarda `X360N_NATIVE_RENDERER=ON` de trabalho Native
   anterior. Reproduzido fora do launcher com `scripts/build-title.ps1 -Title
   dash_17559` (mesma falha em 32 s): defeito do pipeline compartilhado
   (commit `8625338`), não do launcher.
6. **Correção do pipeline:** `cmake/sdk/shared/CMakeLists.txt` passa a recriar
   no `sdk.cmake` os aliases upstream usados pelos alvos do próprio projeto
   (`xxHash::xxhash`, `Vulkan::Headers`, `glslang::SPIRV`), apontando para os
   alvos exportados; um alias cujo alvo não esteja exportado falha já na
   geração do SDK, não no configure de um título. O Native continua sendo
   construído quando o mantenedor o habilita; nada foi desligado.
7. **Fluxo completo após a correção (29/10, 19:31–19:46):** a listagem marcou a
   falha anterior (“Conferir e preparar”); a preparação pelo launcher gerou a
   nova geração do SDK e passou por `sdk-configure`, `sdk-build`,
   `1-configure`, `safe-rebuild`, `2-codegen`, `3-reconfigure` e `4-build` até
   “Preparação concluída” (≈13 min com SDK e título recompilados). O launcher
   iniciou a sessão; a janela da sessão mostrou a dashboard 17559 original em
   PT-BR (Meus Jogos vazio, porque os dados estavam isolados). Um pedido de
   fechamento normal da janela (`WM_CLOSE`, como o botão fechar) levou o
   launcher a “janela fechada” e a “Sessão encerrada” (sucesso) em 41 ms; 3 s
   depois o launcher tinha o foco de entrada e mostrava “Pronta para iniciar”.
   Nenhum processo `x360n`, `dash_17559` ou `x360-native` ficou ativo. As
   gravações da dashboard (cache de shaders, conquistas offline) ficaram
   apenas na pasta isolada `.local/runs/launcher-session-20261029-193105/user`.

Limites: uma execução, com a sessão fechada por mensagem de janela e não por
clique físico; jogo pela My Games, controle e áudio não foram exercitados.

## Nitidez do texto

Relato do mantenedor: textos com aspecto “meio embaçado”. Medido: o processo
é per-monitor DPI aware e roda a 96 DPI, sem escala do Windows. A causa era a
rasterização das fontes: sobreamostragem horizontal 2× com amostragem
bilinear, posições de glifo fracionárias e nenhum hinting/ClearType (o
rasterizador do Dear ImGui é o stb_truetype). Agora: sobreamostragem 1×,
`PixelSnapH` e `RasterizerMultiply` 1,3 para compensar texto claro sobre fundo
escuro. A comparação ampliada 3× das capturas mostra hastes mais firmes e
mais contraste; continua sem hinting nem ClearType, que exigiriam FreeType ou
DirectWrite (não fixados no SDK).
## Não executado / pendente

- **Fluxo real:** importação de ISO/STFS real não foi executada; a sessão da
  dashboard pelo launcher foi validada uma vez no Windows (seção acima), não
  no Linux; jogo pela My Games, controle e áudio não foram exercitados.
- **Extrator em Linux/macOS:** `x360n-extract` só compila no Windows (BCrypt
  para AES/SHA-1, `wmain`, `windows.h` em `system_update.cpp`, fontes `*_win`
  do SDK e DLL do libmspack). A importação de pasta extraída é o mesmo código;
  ISO/GDFX, STFS/PIRS e System Update fora do Windows dependem desse port.
- **macOS e ARM64:** sem host; o target gera `.app` no macOS, não compilado.
- **Distribuição:** `package-desktop.ps1` e `check-package-licenses.ps1` não
  foram executados. O inventário já identifica SDL3, Dear ImGui, Vulkan-Headers
  e as fontes (`licenses/launcher-fonts.txt`, `licenses/vulkan-headers.txt`),
  mas `thirdparty_translation_units`/`sdk_inputs` ainda precisam listar as
  unidades SDL3/ImGui antes de uma Release.
- Troca real de DPI entre monitores, leitor de tela e alto contraste.

Removidos por dependerem de mensagens Win32/IDs de controle do frontend
antigo: `scripts/diagnostics/capture-desktop-ui.ps1`,
`scripts/validation/test-desktop-ui.ps1` e
`scripts/validation/test-library-desktop.ps1`; substituídos pelos testes headless.
Sem Computer Use, push ou Release.
