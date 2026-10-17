# Native N3 — jogos comerciais

Trabalho iniciado em 2026-10-14 sobre `36948a6`. Native D3D12 continua
experimental e independente do executor Xenos. Vulkan permanece indisponível.
Não houve troca do SDK pinado nem novo patch SDK neste incremento.
Código e testes: commit local `c811a69` (`feat(native): enable Castle rendering
and RGB10 guest surfaces`). Sem push ou Release.

## Contratos encontrados e correções

- A intro opcional da Retail 17559 revelou incompatibilidade de assinatura
  VS/GS. O tradutor DXBC empacota TEXCOORDs antes de SV_Position. O gerador de
  retângulos agora mantém essa ordem; teste D3D12 cria pipelines com 0, 1, 4
  e 16 interpoladores. Falhas de PSO registram HRESULT, hashes e estados;
  dump dos shaders só ocorre quando o diagnóstico já foi solicitado.
- Castle usa textura guest RGBA4 (formato 15). Expansão UNORM exata para RGBA8
  e SNORM para float preservam canais, sinal, endian, swizzle e mipmaps. Não
  substituímos a representação signed por zeros quando o tradutor a solicita.
- Forza revelou scanout RGB10A2 (formato 54), tiled 1280×720, com gamma PWL.
  Native aceita os formatos 7/54, mantém os dez bits até a interpolação das
  128 entradas RGB base/delta e usa a saída R10G10B10A2 do presenter. Normalização
  pelo máximo real da LUT (`1023 << 6`), swizzle antes da curva, sem LUT RGBA8
  substituta. Amostragem de textura RGB10A2 usa UNORM10 ou expansão SNORM float
  conforme o binding traduzido. Sem execução de draws pelo plugin Xenos.
- Texturas idênticas eram reconvertidas e enviadas a cada draw. O cache compara
  todos os bytes base/mips com o snapshot que produziu o recurso, incluindo
  escritas CPU e resolves. Chave inclui fetch completo, sinal e dimensão.
  Limite de 256 entradas/256 MiB estimados; referências em voo permanecem vivas
  até o fence mesmo após invalidação/evicção. Comparação não depende de hash.
- Uploads de constantes, índices, vértices e texturas usam páginas mapeadas
  com fatias alinhadas a 512 bytes. Reutilização ocorre somente após fence;
  todas as cópias e CBVs incluem o offset da fatia. Retenção limitada a 64 MiB
  entre submissões. Não se retirou sincronização necessária.
- Captura GPU opt-in compartilhada: duração limitada a 300 s, conjuntos
  separados por processo/entrada e publicação do PPM apenas após escrita
  completa. Default continua 30 s. Capturas são pixels do presenter real.
- `session --skip-boot-intro` permite o boot direto já existente da dashboard
  sem modificar manifesto/recibo. É diagnóstico explícito, não fallback.

## Diagnóstico de lentidão

`native_stats` é desligado por padrão. Contadores de custo cumulativo medem
tempo de parede nos callsites, incluindo esperas; flush está incluído em
resolve, texturas estão incluídas em draw.
Não somar esses números como categorias independentes nem tratá-los como FPS.
`native_texture_cache=false` permite controlar somente o cache no diagnóstico.

Antes do cache, `20261014T222158Z-n3-castle-menu-costs` transferiu cerca de
22 GB de texturas repetidas e acumulou 50,7 s preparando texturas em cerca de
70 s de execução. Depois do cache, o trecho inicial reutilizou milhares de
bindings com dez uploads (~31 MB). Após isso, o custo observado de criar
buffers por draw motivou as páginas de upload. O run
`20261014T222844Z-n3-castle-upload-arena` avançou pela abertura, créditos e
loading originais; capturas estáticas coincidem byte a byte com capturas
anteriores. Esses diagnósticos têm trechos/timings de entrada diferentes:
demonstram eliminação de trabalho repetido, não um ganho global certificado
nem comparação competitiva. O usuário ainda relata lentidão; desempenho
aceitável **não está aprovado**.

## Referência Pinyon Shift

Consultados o pacote-fonte local v0.5.1 e seu `config/release-toolchain.json`,
que fixa [ShiftGlue `1e8968f7`](https://github.com/arcanite24/shiftglue-sdk/tree/1e8968f712824b97d4f4330c5a22389c4c8be1b5),
mais os executores D3D12/Vulkan dessa revisão. Isso é referência, não alteração
da dependência do x360-native. Fontes consultadas ficam em
`.local/reference/pinyon/n3-renderer-review/`.

O executor D3D12 referencia memória e cache de texturas persistentes e oferece
readbacks enfileirados, drenados em ordem no ponto de visibilidade CPU.
Também possui modos específicos que reutilizam resultado anterior ou reduzem
readbacks: esses modos não foram transpostos para o Native. Nosso caminho
ainda espera por cada resolve e copia o resultado para memória guest. Evoluir
residência, invalidação e submissão exige preservar os contratos CPU/GPU, não
apenas desligar readback. A meta de superar Pinyon exige cenas e fidelidade
equivalentes, frame pacing e CPU/GPU/RAM/VRAM medidos; ainda não foi demonstrada.

## Limitações conhecidas

Após corrigir a assinatura de geometria, a intro opcional 17559 chega a
`PM4_EVENT_WRITE_EXT` (0x5A), screen extent query ainda não implementada.
O Native mantém falha explícita 78; não retorna extents fictícios. O fluxo
N3 usa boot direto explicitamente. Outros limites N2 (incluindo oclusão,
memexport e tessellation) permanecem até validação específica.

Runs diretos sob `.local/build/native-n3-diagnostic` não têm recibo de título
e servem apenas para investigar bloqueios; não comprovam entrada via My Games.
Timeout/cleanup não conta como retorno original. A tentativa de cópia com
DLL ainda em uso (~22:24 UTC) executou binário antigo e foi excluída.

## Castle — ciclo real confirmado

Preparações oficiais concluídas com recibos reais:
`n3-final-castle-prepare.log` (build 59 s) e
`n3-final-dashboard-prepare.log` (build 175 s). Os produtores e módulos foram
verificados pelo driver normal; nenhuma edição/bypass de recibo.

Sessão `.local/evidence/validation/native-n3/session-native/`, RTX 4070:

| Etapa | Run | Evidência |
|---|---|---|
| Home → Games → My Games → Castle | `20261014T224649Z-session-0-dash_17559` | Castle selecionado, pedido original de lançamento |
| Castle, seleção de personagem, cena e pausa | `20261014T224741Z-session-1-castle_crashers` | Shaders/draws Native, imagens originais e entrada real |
| Retorno à dashboard e My Games | `20261014T225147Z-session-2-dash_17559` | 1020 bytes importados, flags 0x100, nova navegação |

O mantenedor confirmou estar jogando/testando durante a sessão e acionou o
retorno ("já fiz por você"). Logo, não atribuir todas as transições ao script.
`actions.jsonl` registra somente os comandos automatizados. A tentativa posterior
de enviar teclas S encontrou o Castle já encerrado e não enviou entrada.
O retorno original às 19:51:40 foi `XamLoaderLaunchTitle(<dashboard>, 0x100)`,
com três callbacks de término concluídos, drenagem de áudio e exit 0 em
239440 ms, sem cancelamento/timeout. Janela pai **1443902** e XUID
**E00AD9FD043F47E5** preservados nos três processos; todos inicializaram Native.
SHA-256 do perfil local permaneceu
`81706351ae0f3eb4db42bd52050f86cbf3303bb9463680be67ab46221fb24705`.

Capturas PNG e metadados de origem estão em `session-native/gpu/`: Home,
Castle selecionado, abertura, seleção de personagem, cena, pausa e dashboard
retornada. PPMs separados por entrada preservam a sequência completa. Castle
mostra o modo **trial** do conteúdo original; não houve alteração de licença.
A primeira cena com HUD também está no diagnóstico anterior
`castle-menu-final/gpu/frame-142.ppm`.
Não é certificação de campanha, saves ou desempenho aceitável.

Após o retorno, a My Games recebeu um pedido original para Forza, mas seu
recibo anterior foi recusado (`x360n_sdk_backports.cmake` divergente); nenhum
processo Forza foi iniciado nessa tentativa. Isso encerrou a sessão com código
3, sem invalidar o retorno anterior do Castle. A preparação própria foi realizada
em seguida, conforme a próxima seção.
Os avisos XLIVEBASE/XAM/recursos da dashboard e `update:\\sounds` no Castle
continuam nos relatórios; não são descritos como execução sem erros.

## Forza — lançamento original e bloqueio restante

`n3-forza-prepare.log` registra preparação oficial bem-sucedida: codegen reutilizou
615 fontes sem alteração e build terminou com exit 0. Originais 4D5309C9/0.0.0.10
já verificados foram reutilizados, incluindo SpeechFacade/XMediaFacade próprios.
Na sessão `session-forza`, o run `20261014T230305Z-session-0-dash_17559` recebeu
o pedido original da My Games e iniciou `20261014T230414Z-session-1-forza_horizon`
com Native. Falhou no scanout, exit 78 em 2949 ms; não é evidência de gameplay.

Diagnósticos isolados posteriores identificaram e corrigiram os contratos:

| Run | Resultado |
|---|---|
| `20261014T230734Z-n3-forza-scanout-contract` | fetch formato 54, unsigned, tiled 1280×720, pitch 1280, mip 0 |
| `20261014T231138Z-n3-forza-rgb10` | scanout passou; amostragem do formato 54 ainda ausente |
| `20261014T231234Z-n3-forza-texture10` | amostragem passou; conversão de primitiva ausente |
| `20261014T231256Z-n3-forza-primitive` | rastreio: PointList (1), count 1, não indexado, EDRAM modo 4 |

O dump `forza-primitive/last-draw-registers.bin` preserva
`PA_SU_POINT_SIZE=0x00080008`, `PA_SU_POINT_MINMAX=0x04000010`, viewport
XSCALE=640/YSCALE=−360 e `VGT_DRAW_INITIATOR=0x00010081`.
A próxima implementação precisa cobrir expansão/cobertura dos pontos, tamanhos
constantes e exports por vértice, coordenadas geradas e clipping conforme o estado
real. Não ignoramos o draw nem substituímos sua semântica por um ponto host fixo.
Referência fixada para a continuação: `PipelineCache::CreateDxbcGeometryShader`
e `GetGeometryShaderKey` em `graphics/d3d12/pipeline_cache.cpp`; a geração DXBC
pura pode ser extraída sem instanciar/delegar ao executor Xenos. O estado capturado
define diâmetro constante 1×1, limites por vértice 2–128 e conversão por viewport;
o contrato geral também inclui `output_point_size`, `param_gen_point` e clip/cull.
Menu, carro, pista, HUD, tráfego e controles Forza Native **não demonstrados**.

## Testes focados

Checagem Xenos com o Castle preparado: `20261014T233324Z-n3-xenos-contract`,
capturas em `xenos-prepared/gpu/`, incluindo Press Start, menu e página de modos.
O script enviou Start às 20:33:49 e A às 20:34:02; ambas as transições apareceram
nas capturas GPU. O teste terminou por cleanup limitado a 60 s (wrapper exit 1),
não por retorno original, e não produziu relatório final completo do supervisor.
Logs do título, ações e PPMs anteriores ao cleanup foram preservados.
Sem regressão visual observada no trecho afetado; não equivale a uma suíte geral.

Par original comparável: `session-native/gpu/castle-menu-back.png` e
`xenos-prepared/gpu/xenos-book.png`, ambos 1280×720. A página, fontes e elementos
coincidem visualmente; capturas não sincronizadas têm diferenças de animação.
MAE RGB de 0,187/0,172/0,139 níveis em 255, sem alegação de equivalência exata.
Hashes e limites em `castle-xenos-comparison.json`. A configuração global
permaneceu Native; Xenos foi solicitado explicitamente apenas nesse diagnóstico.

Após as alterações compartilhadas, Castle e dashboard foram preparados novamente
com recibos reais (`n3-rgb10-castle-prepare.log`, `n3-rgb10-dashboard-prepare.log`).
A primeira renovação do Forza foi interrompida durante o produtor e conservou
o marcador de incompleto; não foi contada como build aprovado.
A retomada `n3-rgb10-forza-prepare-retry.log` concluiu com exit 0: produtor 118 s,
codegen 93 s (615 fontes inalteradas), build 242 s e recibo final emitido.
O executável oficialmente preparado foi então executado diretamente por até
15 s: `20261014T234447Z-n3-forza-final-contract` terminou em 3266 ms com exit 78,
`primitive=1, count=1`, sem timeout/fallback. Esse diagnóstico final confirma
o bloqueio no binário entregue; a origem My Games foi demonstrada na sessão
anterior, não atribuída a esta invocação direta.

Testes focados de RGBA4, snapshots/invalidação e linkage de geometria passaram
(194 asserções em quatro casos no executável `x360n_native_tests`). Evidência:
`.local/evidence/logs/n3-cache-tests.log`. Build incremental do upload arena passou;
capturas reais exercitam offsets de constantes, índices e texturas.
Após RGB10/PWL e SNORM, build incremental final e 215 asserções em cinco casos
passaram (`n3-final-native-tests-build.log`, `n3-final-native-assertions.log`).
Uma invocação CTest com alternância `|` foi rejeitada pelo wrapper cmd; não conta
como teste aprovado. A checagem ABI foi executada separadamente.
`n3-final-native-abi.log`: passou, incluindo versão ABI 1, rejeição de Vulkan
sem implementação e ausência de carregamento do plugin Xenos pelo Native.

Seleção global/persistência: 13 asserções em dois casos passaram
(`n3-global-renderer-tests.log`). Não houve suíte completa nem benchmark antigo.
Scanout ampliado: `n3-rgb10-native-probe.log` e `n3-rgb10-xenos-probe.log`,
72 envios/48 capturas por backend, 4.718.592 canais conferidos por execução.
Inclui RGBA8, RGB10A2 nos dois formatos, linear/tiled, quatro endians, BGRA/RGBA
e curvas PWL com slopes diferentes por canal; erro máximo de um nível de 8 bits
para a referência CPU. Cinco pares PNG (frames 0/7/23/31/47) têm hashes idênticos
entre Native e Xenos. É teste sintético de correção, não imagem original de jogo
nem benchmark. Os diretórios são `scanout-native` e `scanout-xenos` sob a evidência N3.

## N3.1 — Forza até a cena jogável (2026-10-14, parcial)

Continuação local a partir do trabalho não commitado deixado pela sessão
anterior (PointList via gerador de GS do SDK fixado, restart de primitivas,
texturas 3D/profundidade/BC4, resolves sem cobertura). Código: `fafbe33`;
script de rota com `hold`/RT/LT: `066d36c`. Sem push ou Release.

**Fato:** pelo executável diagnóstico (`native-n3-diagnostic`, sem recibo,
não é entrada pela My Games), o Forza passou intro, menu, vídeo, carregamento
e cutscenes e chegou à cena dirigível com HUD (minimapa, 2.9 MI, velocímetro)
renderizada pelo Native; freio/ré respondeu (marcha R). Evidências em
`.local/evidence/validation/native-n4/` (`n31-*.png`, hashes em `n31-evidence.txt`;
runs `20261015T0051*Z-n4-forza-c9-deferred`, `c7`, `c8`, `c10`).

Contratos novos, espelhando o executor fixado quando o guest fornece estado
inválido: textura de tipo inválido/sem dados e views incompatíveis ou de sinal
não usado → view nula (draw executa); fetch de vértice inválido é executado
como programado (o `vfetch` está sob predicado no shader, ex. `4E1DA281…`);
`EVENT_WRITE_EXT` grava extents máximos (superconjunto; só afeta predicação);
polygon offset aplicado no teste ROV; DXN → BC5; cube/arrays; sampler conforme
`GetSamplerParameters`.

Desempenho (somente diagnóstico `native_stats`, trechos não idênticos): na cena
3D passou de ~1,2 para ~2,6 presents/s; por quadro, uploads de textura 281→86 e
submissões 380→95. Ganhos vieram de conversão direta nas páginas de upload,
leitura de resolves adiada até pontos observáveis pelo guest, eventos sem
espera quando não há resolve pendente, cache LRU por conteúdo com validação por
época e cópia única de vértices por época. **Injogável; não aprovado.**
Próximo gargalo medido: texturas geradas por resolve fazem GPU→CPU→GPU
(carregamento de texturas na GPU com os shaders `texture_load_*` do SDK é o
passo indicado) e compilação de PSO síncrona.

Pendências: congelamento intermitente na cutscene interna (c8/c10; o c9
passou), artefatos visíveis vs Xenos (lanternas magenta, brilho no vidro
traseiro; causa não identificada), ciclo Dashboard → My Games → Forza →
Dashboard com o plugin novo **não revalidado** (requer nova preparação
oficial). Testes focados: `x360n_native_tests` 18 casos/362.367 asserções.
