# Interface desktop (x360-native)

Gerenciador da plataforma: extrair → abrir → importar → preparar → iniciar o
Xbox 360. A dashboard original continua sendo a experiência de console e o
fluxo de execução (`x360n session`) não mudou. Desde LAUNCHER-UNIFIED-01
(2026-10-29) existe **um único launcher** para Windows, Linux e macOS, com a
identidade visual compartilhada entre plataformas; fatos medidos e
limites ficam no fim.

## Arquitetura

| Camada | Código | Papel |
|---|---|---|
| Estado e regras | `src/desktop/app/app_state.h`, `app_controller.*` | Página, seleção, textos, próxima ação, comandos permitidos, perguntas e eventos dos workers |
| Operações | `src/desktop/app/operations.cpp` | Listagem, importação, `x360n prepare`, sessão, remoção e atualização no worker; eventos postados ao controller |
| Serviços do sistema | `src/desktop/app/environment.h` + `environment_win32.cpp` / `environment_posix.cpp` | CPU/GPU/memória, espaço e permissão, toolchain, ferramentas do sistema, abrir pastas, reiniciar o aplicativo |
| Interface | `src/desktop/ui/` (`frontend.cpp`, `components.cpp`, `pages/*.cpp`) | SDL3 + Dear ImGui; desenha as páginas a partir do estado e enfileira comandos |
| Apresentação | `src/desktop/ui/renderer.cpp`, `soft_raster.cpp` | SDL_GPU (Direct3D 12, Vulkan ou Metal); sem dispositivo GPU, rasterizador próprio na superfície da janela |
| Modelo | `src/desktop/model/` | Catálogo, importação, biblioteca, recibos, atualização — inalterado e compartilhado |

O mesmo target CMake (`x360-native`) gera `x360-native.exe` (Windows),
`x360-native` (Linux) e `x360-native.app` (macOS), também em modo desktop-only
(`X360_DESKTOP_ONLY=ON`), sem o runtime dos títulos. SDL3 e Dear ImGui vêm do
SDK fixado; no build completo o launcher reutiliza o SDL3 do SDK, no
desktop-only configura o mesmo SDL3 com as mesmas opções
(`cmake/desktop/x360n_launcher_deps.cmake`). O launcher abre mesmo quando o
Vulkan do sistema não inicializa os jogos: a apresentação não depende do
renderer da sessão, e o rasterizador de software é o último recurso
(`--renderer software` ou `X360N_LAUNCHER_RENDERER=software` forçam esse caminho).

A preparação Windows com D3D12 inclui os plugins Xenos e Native junto ao
executável de cada título, ambos cobertos pelo recibo de preparação. A escolha
continua nas configurações globais; Native exige acesso experimental e não
recorre ao Xenos quando indisponível. O driver reavalia o padrão ao configurar
builds antigos que guardavam `X360N_NATIVE_RENDERER=OFF` implicitamente;
`--define X360N_NATIVE_RENDERER=OFF` ou a opção explícita no preset continuam
válidos. Builds sem D3D12 e outras plataformas não ativam Native por padrão.

## Identidade visual

- Tema escuro único: grafite (`#0D0F12` página, `#0A0C0E` barra lateral,
  `#161A1F` cards, `#1F242B` controles) e verde Xbox (`#107C10` ação, `#60D660`
  destaque). Estados: verde = pronto, âmbar = atenção/não suportado, vermelho =
  erro, azul = preparando/informação, cinza = importado.
- Uma família embutida: Noto Sans CJK JP Regular/Medium, sem alteração dos
  arquivos originais, busca por fontes do sistema ou mistura de famílias.
  Corpo de 20 px, legendas de 18 px e títulos de 26/36 px. Fonte, hashes,
  licença, custo de tamanho e cobertura em `src/desktop/resources/fonts/README.md`.
  Rasterização stb_truetype, sem FreeType ou reforço artificial de peso.
- Ícones vetoriais de 20 px, com traços e bordas ajustados aos pixels físicos.
  Alertas usam triângulo preenchido e marca central. Texto direto recebe a
  densidade atual do framebuffer; rótulos centralizados usam a altura das
  capitais. Bordas não duplicam o deslocamento de meio pixel, e a faixa de
  antialias permanece de um pixel físico. A escala acompanha o monitor.
- Componentes: botão primário/secundário/fantasma, item de navegação, chip de
  filtro, cartão de opção gráfica, interruptor, selo de estado, card, banner
  de atividade/resultado, barra de progresso por etapas, lista de cartões.
- Hover, selecionado, desabilitado, carregando e foco visível (anel verde de
  2 px, só no uso do teclado ou controle) em todos os controles.

## Telas

| Tela | Conteúdo |
|---|---|
| Início | Estado da dashboard configurada, ação principal (Iniciar Xbox 360/Escolher dashboard), resumo e passos para preparar console, adicionar jogos e jogar pela My Games |
| Biblioteca | Lista de cartões (nome, versão, selo Pronto/Preparando/Importado/Não suportado/Erro), busca por nome/versão (Ctrl+F), filtros Todos/Prontos/Preparar/Atenção, painel da seleção com a ação disponível |
| Importar | Área de soltar arquivo/pasta, ISO/GDFX, XBLA/STFS e pasta extraída; progresso por etapas (Extração → Verificação → Importação) |
| Dashboard | Versões da dashboard original, estado de cada preparação e requisitos (arquivos originais, preparação AOT, ambiente do PC) |
| Configurações | Colunas estáveis: console, aplicativo e armazenamento à esquerda; gráficos e ajustes de imagem à direita. Em janelas estreitas, uma coluna na mesma ordem. Ajustes de imagem recolhidos por padrão. Países traduzidos e ordenados em PT/EN; seletores de país e idioma sem busca, com nomes nativos preservados ([contratos e validação](console-language-locale.md)) |
| Diagnóstico | Indicadores de gráficos/toolchain/armazenamento, abas Ambiente/Conteúdo/Última operação, identificação exata do build, captura de falhas, verificar computador, abrir logs e copiar relatório completo. O alvo da última operação é preservado ao mudar a seleção |

O banner no topo mostra a operação em andamento (título, mensagem, tempo,
etapas, Cancelar) ou o resultado (sucesso/aviso/erro) com **Tentar novamente**,
**Diagnóstico** e **Fechar**. A janela é redimensionável (mínimo 1000×700 na
escala 100 %, proporcional à escala do monitor). Teclado: Tab/setas movem o
foco, Enter ativa, Esc fecha primeiro a lista aberta e só depois volta de
Configurações/Diagnóstico, Ctrl+F busca. Controle:
direcional move o foco, A ativa, B volta; o launcher só lê o controle quando
está em foco e sem operação, porque durante a sessão o controle é do jogo.
Perguntas (downloads, remoção, instalador, pasta de saves) são diálogos
modais do próprio launcher, com a resposta segura como padrão na remoção.
Seleção de arquivo/pasta usa o diálogo nativo do sistema (SDL3: Windows,
portal/zenity no Linux, macOS); arrastar e soltar um item importa.

No Linux/macOS só a API Vulkan aparece (padrão; MoltenVK no macOS); Native fica
desabilitado com o motivo, pois exige Direct3D 12. No Windows o D3D12 é padrão
e Vulkan continua atrás do acesso de desenvolvimento, como o supervisor exige.
Uma escolha incompatível nunca é trocada silenciosamente: é recusada.

## O que o progresso mostra de verdade

Não há percentual de ISO/STFS disponível no extrator, então a barra **não
inventa porcentagem**: etapas concluídas ficam sólidas e a etapa atual anima.
Mostra-se tempo decorrido. A preparação chama o pipeline compartilhado
`x360n prepare <título> --keep-incomplete` (no Windows dentro de
`scripts/devshell.cmd`, que só fixa o ambiente MSVC/LLVM) e mostra suas etapas
reais: Ferramentas (`prepare-tools.ps1` no Windows; ferramentas do PATH no
Linux/macOS), SDK, Configuração, Codegen, Compilação, Verificação (marcadores
`X360N_STAGE` do supervisor) e Registro. Etapas reutilizadas pelo cache contam
como concluídas. Cancelar interrompe o processo; recibos, SHA-256 e retomada
são os do supervisor. Importação continua cancelável; o original nunca é alterado.

## Revisão visual e testes locais (sem Computer Use)

Os testes dirigem o controller compartilhado e desenham o frontend sem janela:
`x360n_launcher_tests "[launcher-layout]"` percorre todas as páginas e banners
em 1000×700, 1220×800, 1280×720, 1600×900 e 1920×1080, nas escalas 100/125/150/200 %
e em PT-BR/EN, conferindo que cada controle fica dentro da janela e do seu card,
sem sobreposição e com o texto cabendo. Com `X360N_LAYOUT_CAPTURE=<pasta>` grava
PNGs pelo rasterizador do próprio launcher, sem captura de tela do sistema.
`X360N_LAYOUT_CAPTURE_QUICK=1` limita a gravação às telas PT-BR em 1220×800,
100 %, com os quatro estados de banner; as verificações continuam completas.

Opções somente de builds fora da distribuição: `--lang pt|en`, `--import
<caminho>` (inicia a importação ao fim da primeira verificação) e `--smoke
<arquivo>` (abre a janela real, desenha 60 quadros, grava renderer, tamanho,
escala e estado inicial e fecha). `--renderer gpu|software` vale em qualquer build.

As capturas locais (`.local/runs`, ignorado pelo Git) podem mostrar caminhos do
usuário, por isso não são versionadas.

## Medições (2026-10-29, Release, ambiente vazio descartável, 10 s após 4 s de abertura)

| | Win32 (build de 27/10) | Unificado, SDL_GPU D3D12 | Unificado, software |
|---|---|---|---|
| CPU ociosa | 0,00 % | 0,00 % | 0,00 % |
| Working set | 26,7 MB | 94,2 MB | 48,4 MB |
| Memória privada | 5,1 MB | 112,4 MB | 28,7 MB |
| Handles | 305 | 546 | 398 |

A interface só redesenha com eventos; durante uma operação anima a ~30 quadros
por segundo. O custo de memória do caminho GPU vem do dispositivo Direct3D 12
do SDL_GPU e é o preço medido da mesma interface nos três sistemas; o
rasterizador de software reduz a memória, mas desenha um quadro de 1220×800
em dezenas de milissegundos e fica como alternativa. Animação durante operação
não foi medida neste incremento.

## Limites conhecidos

- Windows x64 compilado e validado (testes, capturas headless, smoke real com
  D3D12 e software). Linux, macOS e as demais arquiteturas: ver o relatório
  [LAUNCHER-UNIFIED-01](../reports/launcher-unified-01-2026-10-29.md).
- O extrator `x360n-extract` (ISO/GDFX, STFS/PIRS, System Update) compila e
  passa nos testes no Windows e no Linux; no Linux uma importação STFS real pelo
  launcher foi validada. No macOS não foi compilado.
- DPI foi exercitado por escala nos testes e no Windows real a 100 %; troca
  real entre monitores e Wayland fracionário não foram observados.
- Sem modo de alto contraste, sem tema claro; PT-BR e inglês somente.
- Sem validação com leitor de tela (ImGui não expõe árvore de acessibilidade).
- Pacote de distribuição (`X360N_DISTRIBUTION`) e o gate de licenças do pacote
  não foram executados; o inventário precisa listar as unidades SDL3/ImGui
  compiladas (`thirdparty_translation_units`) antes de uma Release.

## Revisão de interface — 31/10/2026

Configurações usa grupos estáveis e ajustes de imagem recolhidos; Início mantém
os três cartões de passos. Textos de uso comum foram encurtados. O Diagnóstico
reúne ambiente, conteúdo e última operação; preserva causas de falhas e
pendências das dependências mesmo após mudar a seleção. Não faz sondagens
nem leituras de arquivos durante o desenho.

O conflito entre o atalho Dashboard e o cartão de passos foi reproduzido com o
verificador de IDs do ImGui ativo. Barra lateral, páginas e faixa da operação
agora têm escopos distintos; o comando continua compartilhado. O teste percorre
os botões visíveis das seis telas e verifica suas identidades ao passar o mouse.

Validação em PT/EN, cinco tamanhos e escalas 100/125/150/200 %, cobrindo páginas,
abas, ajustes expandidos/recolhidos e estados ocioso/preparando/erro/importado.
Bordas e ícones conferidos em pixels a 1x/1,5x/2x. Testes de glifos não aceitam
substitutos. Windows: seis telas e seletores conferidos no aplicativo. Linux
x64: compilação e testes sem janela. macOS e ARM64 não executados; nenhuma
sessão de jogo nem mudança de preferências do console ou do SDK.

Evidências locais em `.local/tools/launcher-review/`: `id-conflict-before.log`
(reprodução), `precommit-final-tests.log` (Windows: 29 casos),
`linux-precommit-final-tests.log` (Linux: 28 casos), `icons-tests.log` e capturas
das telas. Todos os testes finais passaram. O atalho Dashboard do cartão
também foi usado no executável Windows atualizado, sem o aviso de IDs.
