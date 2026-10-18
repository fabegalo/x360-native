# Launcher: responsividade, lançamento e encerramento — 18/10/2026

Base investigada: `main`, `5a57942`. Escopo: frontend desktop, autenticação de
arquivos e supervisor. Sem execução de jogos, Computer Use, screenshots,
alteração de renderer/gameplay, SDK, patches, originais, perfis ou saves.
Build Debug desktop-only pelo `scripts/devshell.cmd`, LLVM pinado 22.1.8.
Implementação e regressões: commit local `6e535cf`.

## Causas e histórico

| Sintoma | Causa demonstrada no código | Correção |
|---|---|---|
| Janela deixa de atender durante carga/atualização | `Load()` fazia `ReadCatalog()` na thread Win32; cada inventário resolve milhares de caminhos. `CheckEnvironment()` também criava dispositivo/procurava ferramentas nessa thread. | Catálogo, inspeção, hardware e preferências são produzidos pelo worker e publicados após conclusão. Navegação usa o snapshot publicado. |
| Interações congelam durante operações | `Start()`, seleção/atualização de ambiente e startup verificavam payload/recibos sincronamente; `WM_TIMER` percorria até 50 mil arquivos extraídos e relia logs inteiros. | Preflight de instalação e atualização em workers; timer só anima e trata entrada. O worker de preparação lê no máximo 64 KiB do log da etapa a cada 500 ms. |
| Lançamento reprocessa os mesmos arquivos | Inspeção de jogos, recibos, biblioteca e `PrepareLibrary()` repetiam SHA de fontes, ferramentas, DLLs e cópias instaladas. O supervisor verificava novamente o perfil inicial, descobria rotas AOT fora do mesmo lote e reabria DLLs para o relatório final. | Um lote protegido por operação reaproveita SHA dos mesmos bytes. Biblioteca já conferida não é preparada outra vez ao lançar. Descoberta AOT e primeiro lançamento compartilham o lote; demais transições autenticam um lote novo. Identidades das DLLs são preservadas para o relatório. |
| Indicador permanece depois de fechar | `finish()` chamava `CheckContents()` para todo o acervo **antes** de postar `kFinished`, mantendo a sessão marcada ativa. Depois, o handler ainda repetia o probe do ambiente. | Sessão posta conclusão sem inspeção global nem novo probe. Preflight inválido desabilita o alvo; falha de execução conserva o erro e permite nova sessão sem exigir rebuild do produto autenticado. |
| Fechamento/falha/cancelamento inconsistentes | Fechamento era um boolean consultado a cada 100 ms; o caminho de debug não consultava cancelamento. A UI só sabia do término do supervisor, inclusive seus relatórios. | Evento da própria janela acorda a espera do filho; debug também cancela. Evento independente informa imediatamente o fechamento ao launcher. Saída do supervisor é observada pelo handle do processo. |

`f216732` introduziu o fluxo desktop. `9ecc073` (12/10) corrigiu hashes durante
navegação, mas acrescentou a reinspeção em `finish()` e manteve o catálogo
síncrono. `f6fee9e` (13/10) acrescentou os recibos de procedência completos.
`dc98dab` (14/10) ampliou catálogos e descoberta de AOT, multiplicando verificações
compartilhadas. `03bc358` acrescentou verificação de payload aos caminhos de UI.
`26de096` ampliou as dependências AOT/recibos. A comparação foi de fontes e
fixtures executáveis; não houve um bisect binário completo de todas as versões.
As validações acrescentadas por esses commits continuam necessárias.

O caminho **Lançar** não executa `build-title.ps1`, CMake, codegen ou checkpoints
incrementais. Eles continuam exclusivos da preparação. Não houve atalho por
timestamp, revisão de SDK, título, basename ou versão de dashboard.

## Autenticação e concorrência

`DigestBatch` é local à thread e à operação. Cada SHA reutilizável mantém um
handle Windows aberto com `FILE_SHARE_READ`, excluindo escrita e remoção. A chave
é volume + identidade do arquivo obtida do handle recém-aberto, não nome/mtime.
Identidade indisponível não é reutilizada. O buffer de leitura também é do lote.
Escritor ou mapeamento gravável existente impede essa leitura protegida.
Outras plataformas continuam lendo os bytes em cada chamada.

Não se persiste aprovação e não se compartilha cache entre desktop/supervisor,
threads ou sessões. Os handles são liberados antes de executar o título; cada
transição recebe outra autenticação. Recibos, globs, anchors, dependências,
marcadores, produtor e isolamento por versão continuam conferidos. Cancelamento
é observado entre arquivos, no inventário e entre blocos de hash.

O worker publica `pending_*`; a UI adota os resultados após conclusão/join.
Pintura e navegação nunca leem vetores que o worker está substituindo. Ações que
modificam o ambiente permanecem serializadas. Minimização, foco e ativação não
sinalizam fechamento. A terminação continua restrita ao Job Object possuído;
nenhum processo é procurado pelo nome para ser encerrado.

Após fechar a janela, o estado passa a **Sessão encerrada / finalizando registros**.
A navegação permanece utilizável. Novo lançamento/preparação aguarda a saída real
do supervisor e suas travas, evitando sobreposição enquanto logs são preservados.
O teste simula 500 ms de cleanup para comprovar essa distinção. Logs reais muito
grandes ainda podem prolongar essa finalização; já não mantêm o indicador de
sessão ativa. Crash conserva saída de erro; fechamento normal conserva saída OK.

## Medições

Evidências locais em `.local/evidence/launcher-2026-10-18/`, incluindo stdout,
falhas iniciais, hashes dos executáveis e traces. O corpus contém quatro perfis,
2.048 assets de 16 KiB e 512 dependências compartilhadas de 256 KiB, com quatro
recibos válidos. Os executáveis de título desse corpus são arquivos sintéticos
que **nunca são executados**. A biblioteca é apenas inspecionada.

| Medição sintética | Antes | Depois |
|---|---:|---:|
| Retorno da carga de catálogo na thread Win32, inventário de 5.000 entradas | 32.235 ms | 16 ms |
| Saída do filho → UI ociosa, sucesso/falha/sucesso, com 6.000 inputs de outro alvo | 1.167 / 1.145 / 1.142 ms | 18 / 20 / 16 ms |
| Lançar → entrada do processo sintético, primeira sessão | 185 ms | 186 ms |
| Mesmo lançamento nas duas sessões seguintes | 81 / 80 ms | 79 / 89 ms |
| Conferência de catálogo/conteúdo/recibos/biblioteca, média de quatro passagens | 21.601,6 ms | 11.560,2 ms |
| Repetições quentes dentro dos mesmos processos, média de duas passagens | 21.660,2 ms | 11.532,1 ms |

A série principal de inventário foi **antes → depois → depois → antes**, dois
passes por processo, sem builds/testes concorrentes e com tracing OFF. Antes:
21.926,2 / 22.144,0 / 21.160,0 / 21.176,3 ms. Depois: 11.458,3 / 11.516,8 /
11.718,4 / 11.547,4 ms. Redução média de **46,5%** nesse corpus; os quatro perfis
continuaram preparados. Cada passagem posterior leu 2.577 arquivos / 167.925.122
bytes e reutilizou 15.919 digests protegidos. Não houve flush do cache do sistema:
inclusive os primeiros passes já tinham dados aquecidos pela criação/leitura do
fixture. Não são medições de cache frio.

O catálogo propriamente dito continuou na ordem de 1,6–1,9 s nesse corpus; não
se atribui ganho ao parser. O ganho de responsividade vem de retirá-lo da UI.
O fixture mínimo de lançamento não demonstra ganho significativo de startup:
185 → 186 ms e 80–81 → 79–89 ms são amostras pequenas, inclusive sem ganho ou
com piora em amostras individuais. A grande diferença é
a conclusão sem reinspeção de conteúdo alheio, e a conferência compartilhada.
Nos testes de janela, fechar → filho encerrado levou 0 ms no caminho normal e
1 ms em debug; debug ainda atende eventos/cancelamento em intervalos de até
100 ms. São tempos observados, não um SLA para qualquer máquina.

O frontend antigo falhou nas três regressões reproduzidas: thread bloqueada,
cancelamento que ainda criava processo e saída sem atualização pronta da UI.
Na coleta final OFF, `desktop.launcher` passou **9 casos / 107 asserções** em
4,79 s. A coleta ON anterior passou 9 / 104 e validou o resumo TSV/JSON; a
diferença de três asserções é a proteção adicional de retry após falha, sem
reconstruir o snapshot entre sessões. A última execução também exerceu retry
depois de navegar para outra página durante a sessão que falhou. Resultados exploratórios anteriores
(25,8/24,3 s antes; 11,2/10,7 s depois) foram preservados, mas a série alternada
sem carga concorrente é a comparação principal.

A medição da UI compila o frontend anterior de `5a57942` contra o mesmo
modelo/núcleo do harness, isolando a mudança da UI. O benchmark anterior de
inventário foi ligado às bibliotecas desktop-only existentes antes das edições;
o posterior usa o mesmo driver e corpus com o lote de verificação. Hashes locais
permitem identificar os binários medidos. Os dados não certificam um pacote
Release ou outro hardware.

Uma tentativa de inventário real foi limitada a 240 s e não completou uma
amostra; foi excluída das comparações. Não houve medição de startup de dashboard
ou jogo real, primeiro swap, FPS, gameplay ou desempenho gráfico nesta tarefa.
Uma primeira autenticação de acervo grande continua limitada pela leitura dos
bytes; o ganho elimina repetições e bloqueio da thread da interface.

## Regressões e reprodução

- `desktop.launcher`: janelas Win32 ocultas e processos sintéticos. Carga com
  inventário de 5.000 entradas, mensagens enquanto o worker opera, cancelamento
  antes de criar processo, sucesso/falha/sucesso, notificação antes do cleanup,
  foco/minimização, fechamento normal/debug, processo alheio sobrevivendo,
  timeout apesar de notificações, hashes protegidos, escritor/mapeamento,
  alteração com tamanho/mtime preservados, threads independentes e caminho
  Unicode maior que MAX_PATH.
- `desktop.launcher-contracts`: 25 casos / 1.090 asserções aprovados; SHA,
  recibos/adulteração, produtor ocupado, saída/crash/timeout, C7/storage/locale,
  avatares vinculados à dashboard e 80 handoffs sintéticos.
- `desktop.catalog`: 82 casos / 53.799 asserções aprovados, incluindo biblioteca,
  importação, payload e isolamento. Primeira execução no TEMP virtualizado do
  sandbox falhou em renames/hardlinks com acesso negado. Repetição com TEMP/TMP
  dentro de `.local/tmp` passou; não se retiraram validações do produto.

O fixture de caminho longo precisou criar sua árvore por caminho estendido;
depois ele verifica o digest usando o caminho normal, exercitando a conversão
do leitor. A tentativa de hardlink no sandbox foi substituída por outro caminho
para o mesmo arquivo; a validação de identidade não exige criar links.

```powershell
scripts\devshell.cmd cmake --build .local/build/desktop-only --target x360-native x360n_launcher_tests x360n_launcher_benchmark x360n_desktop_tests
$taskTemp = Join-Path $PWD '.local/tmp/launcher-tests'
New-Item -ItemType Directory -Path $taskTemp -Force | Out-Null
$env:TEMP = $taskTemp; $env:TMP = $taskTemp
scripts\devshell.cmd ctest --test-dir .local/build/desktop-only -R 'desktop[.]launcher' --output-on-failure
scripts\devshell.cmd ctest --test-dir .local/build/desktop-only -R 'desktop[.]catalog$' --output-on-failure
```

Não foi executada a suíte completa do projeto/SDK. Nenhuma aprovação funcional
de dashboard, C7/C8 comercial ou jogo foi ampliada.

## Roteiro manual: startup real

1. Usar os mesmos binários/recibos válidos, conteúdo, renderer Xenos, preferências,
   perfil, caches e máquina nas duas revisões. Registrar hashes e condições. Se
   um recibo estiver inválido, preparar pelo driver oficial; não certificar um
   produto antigo com recibo improvisado. Anotar separadamente a preparação.
2. Criar uma pasta de coleta nova antes de abrir manualmente o desktop:

   ```powershell
   $trace = Join-Path $PWD '.local/evidence/launcher-manual-01'
   New-Item -ItemType Directory -Path $trace -Force | Out-Null
   $env:X360N_ACTION_TRACE = $trace
   & .local/build/desktop-only/src/desktop/x360-native.exe
   ```

3. Cronometrar a abertura/listagem, navegação/busca/diagnóstico e
   clicar manualmente para abrir a dashboard. Anotar quando a janela aparece,
   quando o conteúdo original aparece e quando aceita entrada. Não confundir
   frame criado, filha anexada e primeiro swap/conteúdo pronto.
   Registrar separadamente a autenticação em **Conferir e iniciar**. **Verificar
   ambiente** pede a varredura completa de forma explícita; não é mais requisito
   para abrir a biblioteca. O estado inicial **Conferir** não certifica os arquivos.
4. Minimizar/restaurar e trocar foco: a sessão deve continuar. Fechar a janela
   native: observar a transição de sessão, finalização dos registros e liberação
   para outro lançamento. Repetir na mesma instalação com cache quente.
5. Se desejar medir jogo, iniciá-lo **manualmente** pela My Games original,
   mantendo o mesmo cenário entre revisões. Não inferir gameplay/FPS a partir
   desses tempos de launcher. Não há execução automática nesse roteiro.
6. Encerrar o desktop e resumir os logs:

   ```powershell
   & scripts/performance/summarize-launcher-trace.ps1 -Directory $trace
   Remove-Item Env:X360N_ACTION_TRACE
   ```

Cada TSV tem `timestamp_monotonic_ms`, `thread_id`, `phase`, `duration_ms`, sem
cabeçalho. Um arquivo por processo. `ui.command.*`, `ui.load`, `ui.start`,
`ui.timer`, `catalog.read`, `catalog.recipe.*`, `catalog.imported.*`, `content.list`,
`content.snapshot`, `receipt.verify.*`, `hash.stream`,
`launch.preflight`, `process.created/exited`, `session.window.create/closed`,
`session.title_window.attached`, `ui.session.closed`, `operation.finish` e
`ui.finished` separam os estágios. Durações de scopes são inclusivas e podem
se sobrepor: não somar seus totais para obter a latência do caminho crítico.
Tracing é opt-in, desligado normalmente; escritas de diagnóstico não alteram
o resultado da operação. ON/OFF não foi certificado como benchmark de gameplay.

## Complemento: um minuto em “Conferindo seu conteúdo”

Após o relato do mantenedor, a base `17b5a0d` foi medida com o acervo local real,
somente para leitura. A primeira correção tirara a carga da thread Win32, mas
ainda exigia autenticar **todos** os títulos antes de publicar a biblioteca.
Havia nove alvos, 11.258 inputs e leitura de 29.499.627.672 bytes por passagem,
incluindo dependências e cópias conferidas pelos recibos. A leitura do catálogo
repetia canonicalização e checagem de ancestrais para milhares de irmãos.

| Etapa no acervo local, Debug e tracing OFF | Antes, passes 0 / 1 | Depois, passes 0 / 1 |
|---|---:|---:|
| Leitura do catálogo | 23.874,9 / 23.484,4 ms | 5.965,16 / 5.967,55 ms |
| Espera inicial de catálogo + estado para a biblioteca | 84.140,3 / 84.881,2 ms | 5.976,74 / 5.979,68 ms |
| Arquivos lidos para SHA / bytes por passagem | 17.396 / 29.499.627.672 | 0 / 0 |

São duas passagens por processo, na mesma máquina/acervo/build Debug, sem build
ou teste concorrente, sem flush do cache do sistema. O segundo passe representa
repetição quente. O “depois” muda deliberadamente o trabalho da abertura:
**lista metadados, sem autenticar o acervo**. Não representa uma aceleração de
93% da autenticação completa. Os 5,98 s não incluem probe de hardware, criação
da janela desktop, preflight de pacote nem startup de dashboard/jogo. Os números
anteriores do corpus sintético permanecem históricos.

`InspectionMode::Preview` nunca define `inputs_ok` ou `prepared` como verdadeiros.
A UI mostra **Verificação pendente / Conferir**, com **Conferir e iniciar** para
sessão candidata ou **Conferir preparação** para conferir o build de um jogo sem
executá-lo. Existência de executável/recibo é apenas metadado. Antes de criar
processo ou executar preparação, o worker usa `Full` (padrão da API) para conferir
SHA-256, recibos, dependências e biblioteca pertinentes. Não há aprovação por
mtime ou cache persistente. Falha publica o estado autenticado negativo. Importar
ou preparar também deixou de disparar uma nova varredura pesada de outros títulos.
**Verificar ambiente** continua pedindo a conferência completa explicitamente.

O catálogo reaproveita resolução de diretórios apenas durante aquela leitura,
com handles `GENERIC_READ` sem compartilhamento de escrita/remoção. Cada folha
tem atributos conferidos; ancestrais também são retidos. Links, ausência de
parent, exclusão indisponível ou host sem suporte seguem as verificações antigas.
Não existe cache global de módulos ou cruzamento de versões. A tentativa inicial
com `FILE_READ_ATTRIBUTES` falhou no teste de exclusão de rename; foi corrigida
para `GENERIC_READ` e o teste passou. Traces opcionais agora distinguem receita,
registro importado e listagem, sem hashes de conteúdo nesta última.

Regressões finais: `desktop.launcher` **14 casos / 150 asserções**, incluindo
conclusão de listagem com 5.000 entradas (1.866 ms; dispatch 24 ms), zero hashes,
conteúdo adulterado com mtime preservado recusado antes do processo, início pela
ação de preview, conferência selecionada sem ferramentas/processo, catálogo vazio
sem loop e exclusão/liberação de diretórios. Sucesso/falha/sucesso sintéticos:
saída → UI ociosa 24 / 26 / 24 ms; fechar janela → filho encerrado 2 / 1 ms
(normal/debug). `desktop.catalog`: **82 / 53.799**, aprovado. Não se repetiram os
contratos de supervisor sem alteração de código daquele componente.

Evidência local: `.local/evidence/launcher-content-followup-2026-10-18/`, com
binário anterior, hashes, stdout/stderr, falha inicial de exclusão, builds,
testes e traces. Reprodução da listagem:

```powershell
& .local/build/desktop-only/src/desktop/x360n_launcher_benchmark.exe . --preview
```

O baseline já recusou recibos de 17559 (fonte `sha256.cpp` alterada) e Forza
(`x360n_sdk_backports.cmake` alterado), encontrou 6717 incompleta e três builds
AOT ausentes. Não se reemitiram recibos nem se recompilaram títulos para esconder
essas pendências. O preview mostra nove pendentes e zero autenticados; isso não
significa que os três preparados anteriores foram removidos. A conferência
completa pode continuar demorada e exigir preparação legítima. Nenhum jogo foi
executado; startup real e desempenho guest permanecem sem medição, com o roteiro
manual acima preparado para essa validação.
