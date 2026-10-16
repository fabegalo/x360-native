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

## Perfil do Guide com dados locais (patch 0080)

A GamerCard original (`XuiGamerCard`, `xam_ui.xex` 17559) mostrava o perfil
sem números. Quatro defeitos, nesta ordem, impediam os dados:

1. **Leitura de imagem síncrona.** O pedido de tile chama a XAM interna
   (`81670C00`/`81670B68`), que falhava antes de qualquer outro pedido. Agora
   esses pontos usam `XamReadTileEx`/`XamReadTile` do SDK, que conclui o
   overlapped com `ERROR_NOT_FOUND` (contas locais não têm gamerpic) e deixa a
   GamerCard seguir.
2. **ABI de `XamUserReadProfileSettingsEx`.** A XAM 17559 passa flags em r9 e o
   ponteiro do tamanho em r10; o SDK lia o tamanho em r9 e devolvia
   `E_INVALIDARG`. Corrigido no patch 0080, com teste das duas ABIs.
3. **realloc privado da XAM (`816E6BB8`).** Com ponteiro nulo ele chegava ao
   heap da XAM, que este host não inicia, e a lista de pedidos da GamerCard não
   crescia (fila vazia, nenhum pedido consultado). Blocos virtuais agora usam o
   heap do SDK, como o free `816E6B10` já fazia.
4. **malloc privado da XAM (`816E6B68`, 14 chamadores).** Mesmo heap ausente.
   A fonte de dados recebia os valores (rastreio temporário: propriedade 22 =
   1), mas não copiava a lista de ouvintes e a mudança não chegava à cena: a
   tela continuava em "Games 0". Ligado ao heap do SDK, par do free acima.

Os três números vêm de `XamUserReadProfileSettings` (CRED, TITLES_PLAYED,
ACHIEVEMENTS_EARNED), sem valor inventado:

- **Jogos:** contagem de títulos da conta conectada, gravada em
  `<perfis>/<XUID>/titles-played.dat` quando um jogo inicia com ela conectada.
  A dashboard e outros títulos de sistema (`FFFExxxx`) não contam.
- **Gamerscore e conquistas:** 0. Não há registro de desbloqueios por conta.
  O enumerador interno de conquistas (`81670190`) continua falhando: o do SDK
  ignora o Title ID pedido e devolveria as conquistas do executável atual, o
  que mostraria dados de outro título como se fossem do perfil.
- **Gamerpic:** nenhuma. O título padrão da chave (`FFFE07D1`) só contém a
  imagem `0x8000`; não há imagem original de gamerpic disponível para uma conta
  local. A área fica vazia, como numa conta sem imagem.

Execuções em `.local/validation/profile/`, Windows x64/Xenos, mesmo build,
diagnóstico sem instrumentação temporária. Capturas conferidas:

| Diretório | Resultado |
|---|---|
| `final-1-forza` | Forza, perfil "Forza": Guide → Perfil mostra Games 1; B volta ao Guide; Guide fecha e o jogo segue. Log: `title 4D5309C9 played`. |
| `final-2b-castle` | Castle Crashers com o perfil anterior: Games 2; B e fechamento devolvem o menu do jogo. Log: `title 584108B7 played`. |
| `final-3-dash` | Dashboard reiniciada com esse perfil: Games 2 (persistência; a dashboard não conta). Arquivo com exatamente `4D5309C9` e `584108B7`. Guide → Perfil → B → Guide → fechar. |
| `switch-1-castle` | Castle com dois perfis (Player1 conectado): registra o jogo só para Player1. Cache frio, o Guide abriu tarde; usado apenas para gerar o dado. |
| `switch-6-dash` | Mesmo processo: Player1 com Games 1 → Sign Out/Yes → Sign In → Player2 → diálogo original de privacidade (No) → Perfil do Player2 com Games 0 → B → Guide. |

`final-2-castle` falhou no runner: a captura passou de 5 s e o encerramento do
auxiliar disputou com o `taskkill` (o título saiu com código 0). Repetido em
`final-2b-castle`. Testes: `x360n_sdk_runtime_checks` com os filtros de perfil,
Guide e entrada, 39 casos/914 asserções (`final-tests.log`).

## Ainda falha ou não foi verificado

- Perfil: gamerpic ausente (sem imagem original para conta local); gamerscore
  e conquistas ficam 0 até existir registro de desbloqueios por conta; os
  troféus bloqueados são o estado padrão da cena. O símbolo de gamerscore
  (`U+E00A`) aparece como retângulo: não existe nas fontes XenonJKLatin,
  XenonCLatin e XenonSCLatin disponíveis. Contagem de jogos não verificada em
  jogos iniciados fora deste fluxo (sem `--x360n_profile_root`).
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
