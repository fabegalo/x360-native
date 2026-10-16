# Guide original 17559 — composição universal pelo buffer de sistema

Execuções locais de 16/10/2026, Windows x64/Xenos (D3D12). Continua o
[relatório de 15/10](guide-composition-2026-10-15.md).

O Guide deixou de usar o dispositivo D3D do título e os hooks de desenho por
jogo. Agora usa o caminho original da XAM 17559: dispositivo de gravação próprio,
compositor `816CCF00` e publicação por `VdSetSystemCommandBuffer`, executada pelo
`VdSwap` de qualquer título. Native continua suspenso; Vulkan não foi exercitado.
Não houve Computer Use, push ou Release.

## Implementação

**SDK — patch 0078** (`patches/rexglue-sdk/README.md`):

- `VdSetSystemCommandBuffer` guarda o descritor de 0x94 bytes montado por
  `81842B08`; `VdQuerySystemCommandBuffer` só libera nova gravação com no máximo
  um buffer publicado pendente (a XAM alterna três), com espera limitada a 100 ms.
- `VdGet3dVideoFormat` retorna 0 (saída sem estéreo). Antes era stub sem r3, e
  `816C8010` compara esse valor com 3.
- `SetSystemSwapCallback`: o host executa o quadro de sistema na thread do
  título, dentro do `VdSwap`. Vale para todo título, sem hook de título.
- Pacote `PM4_XE_SYSTEM_COMMANDS`: o command processor guarda registradores,
  shaders ativos e bin masks do título. Em seguida, coloca o front buffer do
  título no fetch constant 15, executa o buffer gravado pela XAM e restaura o
  estado do título. O swap apresenta o front buffer de sistema do descritor.

**Host 17559** (`titles/dash_17559/src/system_ui.cpp`):

- Reproduz a inicialização estática de `816CE8B0`: cabeçalhos dos eventos e
  listas em `81AA1808/1810`, cuja ausência causava leitura nula em `816C9870`.
- Cria o dispositivo XAM com `816C9EA0`, entrega à rotina de notificação
  original `816CBFD8` as informações de display e escala (tipos 3 e 1) e cria
  os recursos do compositor com `816CA748`.
- Por quadro, quando `VdQuerySystemCommandBuffer` libera, chama `816CCF00` com
  o alvo `81AA1B18`. Mostrar/ocultar usa as variáveis originais de visibilidade
  e fade (`+168`, `+212/+216`) e `816C9A20` publica o descritor vazio.
- Pilha de apps de sistema: um app que abre outro (Guide → Perfil) é serializado
  por seu handler `80000009` e restaurado com `flags=2` ao fechar o sucessor.
- Removidos `titles/forza_horizon/src/system_ui_frame.cpp`, o hook da dashboard
  `sub_9221CD90` e o export `X360nSystemUiFrame`. O supervisor passa o host da
  dashboard da sessão a todo título. Uma dashboard sem host não oferece Guide;
  módulos de outra versão não são usados.

### Fatos, inferências e hipóteses

- **Inferência confirmada pela imagem:** inherit tags `0x10000` na palavra 3
  (`816C9FB4`), tratadas por `81842C08` → `81845860`, correspondem ao fetch
  constant 15. Com fetch 0, o fundo ficou preto (`dash-4`); com fetch 15, o
  título aparece escurecido (`dash-5`).
- **Hipótese documentada:** o bit `0x200` de `XboxHardwareInfo` (xboxkrnl 0x156)
  só é testado em `816C9EA0`, antes de criar o dispositivo de sistema. O runtime
  não o define; o host expõe o bit apenas durante essa chamada e restaura o valor.
  Os títulos continuam vendo `0x20`. O significado exato do bit não foi verificado.
- **Hipótese:** layout da notificação tipo 1, 64 bytes: largura/altura do front
  buffer, do display e parâmetros do scaler. Imagem centralizada nas capturas.
- O canvas original de sistema é 852×480 (`816C8FC0`): durante o Guide, a imagem
  composta é escalada para a janela, como no compositor original.
- O fetch apresentado usa base, formato e dimensões do descritor; os demais
  campos vêm do fetch do título. As cores ficaram corretas nesta configuração.

## Evidências

Diretório local: `.local/validation/system-composition/`. Capturas, binários e
logs ficam fora do Git. Runner: `scripts/validate-original-guide.ps1`, agora
compatível com Windows PowerShell 5.1; esta máquina não tem PowerShell 7.

| Execução | Resultado |
| --- | --- |
| `dash-3` / `dash-debug-1` | Dispositivo e recursos criados; acesso nulo em `816C9870` (`crash.txt`), corrigido pela inicialização de `816CE8B0` |
| `dash-4` | Guide composto pelo buffer de sistema, mas fundo preto (fetch 0) |
| `dash-5` | Título escurecido atrás do Guide; aba Media; fechamento por B |
| `dash-full` | 52,45 s, 20 eventos, saída 0: abas, B, reabertura e fechamento pelo botão Guide; sem sobreposição de texto da dashboard |
| `dash-profile-2` | Settings → Profile → B volta ao Guide na aba Settings com Profile selecionado (`suspended`/`restored` no log); G fecha |
| `forza-1` | Textos completos no Guide; navegação; fechamento com o jogo animando; diálogo Xbox Home com opções legíveis; saída 0, handoff `Dashboard` (4) |
| `forza-driving` | Cenas 3D em andamento aos 235 s, Guide sobre a cena escurecida, aba Media, fechamento; cena seguinte aos 270 s. Não comprovado como condução |
| `castle-1` | Guide aparece pela primeira vez (antes não aparecia), com o jogo escurecido; B fecha e o jogo segue animando; saída 0 |

Todos os builds usados (`dash-build-7`, `forza-build-1`, `castle-build-1`)
terminaram com saída 0. `dash-profile-2` inicial, rodado após um build com erro,
foi descartado e repetido com o binário correto.

Testes focados (Release, TEMP/TMP no workspace):

- `x360n_sdk_runtime_checks` com `[system-command-buffer],[guide-context],
  [guide-contract],[title-return],[profile-ui],[signin],[persist-display]`:
  25 casos, 534 asserções aprovadas (`contracts.log`). O caso novo cobre callback,
  pacote, fetch herdado, fetch apresentado, limite de buffers e retorno ao
  título. Sem o patch, não compila: API e exports ausentes.
- `x360n_tests [session]`: 10 casos, 33.016 asserções aprovadas
  (`session-tests-2.log`). A primeira execução (`session-tests.log`) falhou
  porque o supervisor passou a exigir host em toda sessão. O supervisor foi
  corrigido para não exigir host.
- `apply-sdk-patches.ps1 -Check`: 77 patches aplicados (0001–0078, 0073 retirado).

## Entrada do jogo com o Guide aberto (patch 0079)

Antes desta etapa, a SDK já entregava estado vazio ao título enquanto a UI de
sistema estava visível. O teste novo `system_ui_input_test` reproduziu três
falhas restantes: o título via o bit do botão Guide (habilitado pelo host para
uso próprio); o B que fecha o Guide, ainda pressionado, aparecia ao jogo como
pressão nova; e o soltar dessa tecla chegava como keystroke ao título. O patch
0079 corrige as três, sem caso por título. O teste passou a aprovar: 24 casos e
540 asserções com os contratos do Guide, mais `[mnk-keystroke]` e
`[persist-display]` (`input-test-after.log`, `input-mnk-tests*.log`).

Ao vivo, no Castle Crashers, o título só lê `XamInputGetState`:

- `castle-input-1`: Start/A com o Guide aberto não tiraram o jogo de
  "Press Start"; depois de fechar, Start abriu o menu. Execução feita antes da
  SDK nova; o comportamento é o mesmo depois dela.
- `castle-diag2`, com instrumentação temporária já removida: o B que fechou o
  Guide ficou pressionado por cerca de 0,85 s depois do fechamento e não chegou
  ao jogo; o Start seguinte chegou (`buttons=0010`, `visible=false`).
- `castle-trace-diag`: nenhuma entrada não nula chegou ao jogo durante todo o
  Guide, inclusive com o diálogo Xbox Home. O jogo foi sozinho ao menu
  principal enquanto o Guide estava aberto; a causa provável é o próprio
  título (notificação de UI de sistema ou tempo de attract), não verificada.
- No Guide, Start age como seleção e abre o diálogo Xbox Home; o primeiro B
  fecha o diálogo e o segundo fecha o Guide.
- `castle-input-4`, `dash-full-input` e `forza-input`, com SDK 0079: saída 0;
  Forza com handoff Dashboard.
- `castle-trace`: encerramento forçado pelo runner depois do fechamento da
  janela. Não se repetiu em `castle-trace-diag`, `castle-diag2`, no mesmo
  cenário com o build final (`castle-trace-final`, saída 0) nem nas demais
  execuções. A causa não foi investigada e o caso continua registrado como
  falha.

## Ainda falha ou não foi verificado

- Perfil: gamerpic e números de jogos/gamerscore/conquistas não aparecem; o log
  registra configurações de perfil não implementadas. O problema está nos dados
  do perfil, não na composição.
- Entre um app e o seguinte (Guide → Perfil), o quadro do título aparece sem
  escurecimento durante um swap, porque o descritor vazio é publicado ao fechar.
- Tampouco foram verificados animações de fade quadro a quadro, outros idiomas,
  Vulkan, outros jogos e títulos que dependam do conteúdo da EDRAM entre swaps
  (a EDRAM é compartilhada, como no console).
- O retângulo preto no canto inferior direito da dashboard já existia e não
  muda com o Guide.
- `VdRegisterXamGraphicsNotification` continua stub; o host entrega as
  notificações de display e escala uma vez, ao inicializar. Troca de modo de
  vídeo durante o Guide não foi tratada.
- Não houve medição de performance. Com o Guide fechado, o custo por quadro
  continua sendo a verificação do botão Guide no `VdSwap`, como nos hooks
  anteriores. Com o Guide aberto, há o compositor da XAM e uma cópia do
  arquivo de registradores por quadro. Nenhum ganho ou custo foi medido.
- Sessão completa pelo supervisor (dashboard → jogo → Guide → Xbox Home →
  dashboard) não foi repetida nesta revisão; o runner não substitui esse teste.
