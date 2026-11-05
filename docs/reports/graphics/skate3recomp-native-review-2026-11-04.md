# Melhorias do Native a partir do skate3recomp

04/11/2026. Análise documental sobre o x360-native `7d042a69f494862d57739074c393b21c2a3e9724`.
As propostas abaixo não foram implementadas nem medidas nesta revisão.

O melhor aproveitamento está na gestão de recursos e na atribuição de custos:
reutilização de texturas Vulkan, descarte de cache com custo limitado,
subalocação de memória e perfil de GPU por operação. O renderer completo do
skate3recomp não é uma substituição direta para o nosso Native: ele conhece a
engine do Skate 3 e substitui sua cena; o nosso executa comandos e shaders
Xenos de jogos e módulos originais arbitrários.

## Fontes e diferença de arquitetura

Revisões fixadas para permitir repetir a comparação:

- [skate3recomp](https://github.com/mchughalex/skate3recomp/tree/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd),
  `f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd`, confirmado como `main` na consulta.
- [Fork ReXGlue usado pelo projeto](https://github.com/mchughalex/rexglue-skate3/tree/7eb0faf7787f5e01333c228b8e3f03c32f7295ea),
  `7eb0faf7787f5e01333c228b8e3f03c32f7295ea`, obtido do gitlink
  `third_party/rexglue-sdk` daquela revisão. A origem está no
  [.gitmodules](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/.gitmodules).
- Nosso SDK permanece ReXGlue v0.10.0,
  `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, com os patches de
  [deps.lock.json](../../../deps.lock.json). Nenhuma troca de dependência é proposta.

| Camada | skate3recomp | x360-native |
|---|---|---|
| Entrada do renderer | Hooks em funções da engine capturam meshes, listas de cena, materiais, câmera e constantes | Command processor interpreta PM4 e estado Xenos |
| Shaders | HLSL de materiais e passes conhecidos do Skate 3 | Tradução dos microcódigos originais via tradutores do SDK |
| Execução host | RHI D3D12/Vulkan apoiada nos command processors do fork | Backends próprios na fronteira `ScanoutBackend` |
| Redução de trabalho | Pode substituir passes que sua cena nativa reproduz | Precisa preservar todos os efeitos observáveis dos comandos guest |

No upstream, `sub_82795AD8`, `sub_827FAF50` e `sub_82B82E08` são exemplos de
hooks de submissão e fim de frame; as constantes dinâmicas são capturadas depois
do draw original. A cena publicada é um `shared_ptr<const FrameScene>` para
evitar cópia profunda entre threads. Isso descreve um renderer de engine,
não apenas outro backend de uma GPU genérica.
[Hooks](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/src/skate3_native_render.cpp#L382-L438),
[publicação da cena](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/src/skate3_native_scene_state.h#L31-L45).
No nosso lado, ver [contrato do backend](../../../src/native/commands/scanout_backend.h)
e [fontes compartilhadas com o SDK](../../../src/native/CMakeLists.txt).

Os ganhos anunciados no [README upstream](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/README.md)
pertencem a essa substituição específica. O próprio projeto registra limitações
em customização, Hall of Meat e parques. Não equivalem a uma medição do nosso
Native nem comprovam paridade completa com o console.

## Encaixes propostos

A prioridade indica a ordem sugerida de investigação, condicionada ao custo
medido no nosso workload; não uma promessa de ganho.

| Prioridade | Encaixe | Diferença concreta no nosso código | Limite principal |
|---|---|---|---|
| 1 | GPU por operação | Timestamps atuais medem submissões; faltam intervalos GPU por classe de trabalho | Instrumentação opt-in, sem novas esperas |
| 2 | Reutilizar texturas Vulkan invalidadas | D3D12 atualiza o recurso; Vulkan retira e recria imagem, memória e view | Preservar conteúdo anterior até seu consumo e a ordem de barreiras |
| 3 | Descarte de cache com custo limitado | Ambos procuram o mais antigo com `min_element` para cada remoção | Limites de memória e recursos em voo continuam obrigatórios |
| 4 | Subalocação Vulkan | Texturas e buffers têm chamadas próprias a `vkAllocateMemory` | Medir alocações, fragmentação, memória e falhas |
| 5 | Conversão CPU com menos cópias | Vulkan converte para vetor e depois copia ao upload; D3D12 já converte na arena | Não ler memória write-combined durante a conversão |
| 6 | Preparação CPU paralela | Conversões restantes ocorrem no caminho de consumo | Snapshot coerente, espera no primeiro uso e fila limitada |
| 7 | Shaders auxiliares reproduzíveis | Já embutimos scanout; dá para ampliar identidade e geração dos utilitários | Não substituir shaders guest por materiais fixos |

### 1 GPU por operação

**Fato upstream:** a RHI oferece `ProfileRegion` e classes como commit, cena,
resolve e pós-processamento. O perfil pode ser desligado. A própria interface
explicita que a cauda do frame tem atribuição diferente nas duas APIs.
[ProfileStage e ProfileRegion](https://github.com/mchughalex/rexglue-skate3/blob/7eb0faf7787f5e01333c228b8e3f03c32f7295ea/include/rex/graphics/native_rhi.h#L402-L419).

**Encaixe:** ampliar os timestamps de
[D3D12](../../../src/native/d3d12/d3d12_render.cpp) e
[Vulkan](../../../src/native/vulkan/vulkan_core.cpp), reaproveitando
`native_stats`/`native_vulkan_perf` e
[BackendCounters](../../../src/native/commands/scanout_backend.h).
Usar categorias conhecidas sem reconhecer cenas do jogo: carga/conversão de
texturas, draws, resolve, MEMEXPORT, cópias para publicação e scanout.
Separar tempo de GPU, gravação CPU, espera de fence e trecho não atribuído;
intervalos sobrepostos não podem ser somados como tempo exclusivo.

**Aceite futuro:** ler queries apenas após a fence correspondente, tratar
indisponibilidade explicitamente e comparar custo com perfil desligado/ligado.
Integrar ao [replay existente](../performance/render-core-2026-11-01.md), que já
separa fases CPU; não chamar a duração CPU de `Draw` de duração GPU do draw.
Essa atribuição ajudaria a decidir quais propostas seguintes merecem execução.

### 2 Atualizar texturas Vulkan sem recriar o recurso

**Fato upstream:** `UpdateGuestTexture2DInPlace` reutiliza textura, footprint e
SRV para conteúdo 2D de layout idêntico, com escopo restrito e tratamento de
staging. Não é um atualizador universal de texturas.
[Implementação](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/src/skate3_native_scene_gpu.cpp#L2022-L2135).

**Fato local:** em [d3d12_textures.cpp](../../../src/native/d3d12/d3d12_textures.cpp),
`CachedTexture` invalidada mantém o recurso e grava transições/cópias na ordem
dos draws. Em [vulkan_textures.cpp](../../../src/native/vulkan/vulkan_textures.cpp),
a entrada invalidada é retirada e retida pela submissão; uma nova
`TextureResource` cria imagem, memória e view.

**Encaixe:** aproximar Vulkan do contrato já usado no D3D12 para identidade e
layout compatíveis. Reutilizar a imagem quando for possível ordenar a nova
cópia depois dos consumidores anteriores e antes dos posteriores, preservando
layouts, subrecursos, watches e referências de descritores em voo. Mudança de
formato, dimensão, mips, signedness ou escala continua exigindo recurso adequado.
Não transportar a classificação de vídeo/UI do Skate 3 nem suas sondas amostrais
de conteúdo: nossa invalidação precisa continuar cobrindo as escritas reais.

**Aceite futuro:** sequência draw com bytes A → alteração → draw com bytes B,
na mesma lista e entre submissões; ambos os resultados precisam coincidir com o
baseline. Cobrir CPU writes, resolve/MEMEXPORT, aliases, mips/cube/volume e
mudança de layout. Medir criação de recursos/views, alocações, bytes retidos,
tempo CPU e P95/P99 em atualizações repetidas. A economia de alocações ainda
não demonstra ganho de frame.

### 3 Descarte de cache previsível sob pressão

**Fato upstream:** `EvictTexStore` usa lotes limitados, marcas alta/baixa de
ocupação, idade e seleção parcial dos candidatos. Seu orçamento em bytes é
estimado e pode permanecer excedido quando o conjunto é recente; a seleção
ainda percorre o cache. Portanto, não é uma solução pronta de custo constante
nem um limite rígido de VRAM.
[Evicção e orçamento](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/src/skate3_native_scene_gpu.cpp#L256-L411).

**Fato local:** os caches de texturas
[D3D12](../../../src/native/d3d12/d3d12_textures.cpp) e
[Vulkan](../../../src/native/vulkan/vulkan_textures.cpp) limitam entradas a
4.096 e usam orçamento de 1 GiB. Cada remoção procura a entrada mais antiga
com `min_element`; uma inserção que exija K remoções pode percorrer o cache K
vezes. O orçamento do cache não representa toda a memória de recursos retidos
por submissões.

**Encaixe:** comparar LRU mantida incrementalmente ou seleção única de um lote
com o caminho atual. Antecipar descarte de entradas frias em fronteiras seguras,
com orçamento de trabalho, mantendo um caminho obrigatório para atender ao
limite de admissão. Não copiar os tempos de retenção, os tamanhos nem a permissão
upstream de exceder o orçamento. Contabilizar cache e recursos aposentados
separadamente; remover uma entrada não libera imediatamente memória em voo.

**Aceite futuro:** carga com entradas de tamanhos diferentes, invalidação e
reuso de endereços, referências ainda em voo e pressão acima do orçamento.
Comparar buscas, hits/misses, reuploads, memória e latência máxima de inserção.
O custo de manter a LRU nos hits também entra no A/B; sem melhora, manter o
caminho atual. Não há evidência nesta revisão de que evicção domine o Skate 3.

### 4 Subalocar memória Vulkan

**Fato upstream:** a RHI usa `CreateVmaAllocator` com sincronização interna
para criação de recursos por workers, e destruição associada à submissão.
[Allocator e threads](https://github.com/mchughalex/rexglue-skate3/blob/7eb0faf7787f5e01333c228b8e3f03c32f7295ea/src/graphics/vulkan/native_rhi_vulkan.cpp#L490-L525).

**Fato local:** `TextureResource` em
[vulkan_textures.cpp](../../../src/native/vulkan/vulkan_textures.cpp) e
`VulkanBuffer` em [vulkan_core.cpp](../../../src/native/vulkan/vulkan_core.cpp)
fazem alocações próprias. O SDK pinado já oferece
[`CreateVmaAllocator`](../../../external/rexglue-sdk/include/rex/ui/vulkan/mem_alloc.h);
não é necessário importar o fork inteiro para investigar esse mecanismo.

**Encaixe:** começar pelas imagens do cache, medindo subalocação versus
alocação individual, e só ampliar para buffers se o resultado justificar.
Preservar alinhamentos, tipos de memória, recursos que exijam alocação dedicada,
visibilidade host e destruição após o último uso. Ajustar a contabilidade:
bytes do recurso, blocos reservados pelo allocator e memória em voo são medidas
distintas. VMA não elimina a sincronização nem resolve invalidação guest.

**Aceite futuro:** criação/liberação em rajadas, recursos grandes/pequenos,
rollback de falhas e teardown sem recursos pendentes. Medir número de alocações
do driver, tempo CPU, pico/reserva de memória e fragmentação. Essa proposta tem
impacto maior que reutilizar uma textura e deve ser avaliada separadamente.

### 5 Reduzir cópias na conversão CPU de texturas

**Fato upstream:** os uploads distinguem memória write-combined de scratch
cacheável. A conversão que precisa reler linhas usa scratch antes de escrever
o upload; buffers substituídos só voltam ao pool após a submissão terminar.
[Conversão e staging](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/src/skate3_native_scene_gpu.cpp#L2120-L2152),
[pool com aposentadoria](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/src/skate3_native_scene_gpu.cpp#L685-L728).

**Encaixe:** no fallback CPU de
[vulkan_textures.cpp](../../../src/native/vulkan/vulkan_textures.cpp), avaliar
reservar uma fatia da arena e converter diretamente nela, evitando vetor
temporário completo seguido de `Upload`/cópia. O D3D12 já faz isso com `Reserve`.
Só aplicar onde o conversor escreve sem reler o destino; operações que releem
dados conservam scratch cacheável. Reutilizar as arenas existentes, sem criar
outro pool equivalente ao do upstream.

**Aceite futuro:** comparar todos os bytes/subrecursos com
[ConvertTextureCpu e contratos de carga](../../../tests/native/texture_load_test.cpp),
incluindo padding, endian, compressão, volume, mips e escala. Medir alocações
temporárias, bytes copiados e CPU nas conversões realmente usadas. Os formatos
já carregados por compute não devem regredir para conversão CPU.

### 6 Preparar conversões CPU em paralelo com dados imutáveis

**Fato upstream:** workers recebem snapshots, priorizam misses visíveis e
publicam resultados no render thread. Entretanto, o desenho admite desenhar
texturas brancas, omitir conteúdo ou usar o decode anterior enquanto espera;
também usa threads detached.
[Filas e snapshots](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/src/skate3_native_scene_state.h#L84-L165),
[workers e commit](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/src/skate3_native_scene_gpu.cpp#L4740-L4810).

**Encaixe condicionado:** somente se o perfil mostrar conversão CPU relevante,
investigar jobs de conversão de bindings já conhecidos, sobre cópias coerentes
dos bytes consumidos, com fila e memória limitadas. Não pressupor que o PM4
informa antecipadamente o que será usado ou que páginas GPU já estão na CPU.
A geração do recurso e as dependências precisam acompanhar o job; no consumo,
esperar o resultado exato e ordenar uploads/barreiras antes do draw. Uma
escrita posterior exige preservar a versão daquele consumo, não publicar
bytes de outra geração. Workers precisam de cancelamento e join no teardown.

**Aceite futuro:** jobs fora de ordem, CPU/GPU alterando a origem, endereço
reutilizado, erro, fila cheia e encerramento. Sem frames atrasados, substitutos
brancos ou draws omitidos. O custo de snapshots, contenção e espera pode
anular o benefício; começar com formatos CPU e manter compute existente.
O [GuestMemoryMirror](../../../src/native/memory/guest_memory_mirror.h) já tem
epochs e regras de publicação que devem ser preservados, não contornados.

### 7 Identidade reproduzível dos shaders auxiliares

**Fato upstream:** `EmbedShaders.cmake` expande includes antes de embutir o
HLSL, para que o texto completo participe da identidade do bytecode. O projeto
também mantém SPIR-V pré-compilado para seus shaders conhecidos.
[Geração do HLSL](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/cmake/EmbedShaders.cmake),
[fluxo de build](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/README.md#building-from-source).

**Encaixe:** ampliar nosso mecanismo de
[scanout embutido](../../../src/native/CMakeLists.txt) para utilitários host
quando houver duplicação ou custo de compilação demonstrado. Uma geração offline
deve registrar fonte/includes, entrypoint, opções, compilador fixado e hashes
de DXBC/SPIR-V, com regeneração verificável. Não copiar binários upstream como
fonte de verdade. O conjunto dinâmico de shaders guest continua traduzido;
os caches de PSO existentes não são substituídos por esse empacotamento.

**Aceite futuro:** mudar um include precisa invalidar o artefato correspondente;
comparar saída e custo frio/quente. Em scanout, confirmar LUT/gamma, overlay e
dimensões com o replay composto existente. Prioridade baixa enquanto o custo
dos utilitários não estiver separado do custo dos shaders guest.

## O que já temos e deve ser preservado

| Ideia vista no upstream | Estado local e consequência |
|---|---|
| Fronteira D3D12/Vulkan | `ScanoutBackend` já isola as APIs. A RHI upstream usa os command processors do fork; importá-la não seria uma troca isolada |
| Uploads e descritores com vida por submissão | Arenas D3D12/Vulkan já reutilizam páginas e protegem recursos com fences; ampliar pontualmente |
| Redução de estado e uploads redundantes | Já há reutilização de SRVs/samplers, constantes D3D12 e constantes/sets Vulkan |
| Preparação de pipelines | `PipelineWorkers`, comandos diferidos ordenados e caches persistentes já existem; não recriar nem pular draws pendentes |
| Diagnóstico de frames | Captura/replay `X360NFC1` v3 e A/B já verificam memória, contadores e imagem composta; expandir essa base |
| Aspecto e timing separados | Native Enhanced já tem contratos próprios; a câmera e o framerate do Skate 3 não são mecanismos genéricos |

Fontes locais: [arenas D3D12](../../../src/native/d3d12/d3d12_render.h),
[arenas Vulkan](../../../src/native/vulkan/vulkan_render.h),
[workers](../../../src/native/pipeline/pipeline_workers.h),
[PSO D3D12](../../../src/native/pipeline/pipeline_store.h),
[RENDER-FINAL](../performance/render-final-2026-10-31.md),
[PERF-GPU-02](../performance/perf-gpu-02-windows-2026-10-31.md),
[RENDER-CORE](../performance/render-core-2026-11-01.md) e
[Native Enhanced](../../native-enhanced.md).

## O que não encaixa no Native compartilhado

- **Supressão por pitch ou ausência de pixel shader.** O fork preserva certas
  superfícies e suprime outras usando dimensões conhecidas do Skate 3,
  inclusive `1152`, `1024` e `512`. Esses números não comprovam que o trabalho
  é dispensável em outro título. Resolve, depth/stencil, ZPD e MEMEXPORT podem
  ser observáveis sem produzir a imagem final.
  [Política específica](https://github.com/mchughalex/rexglue-skate3/blob/7eb0faf7787f5e01333c228b8e3f03c32f7295ea/src/graphics/native_guest_renderer.cpp#L9-L49),
  [classificação por pitch](https://github.com/mchughalex/rexglue-skate3/blob/7eb0faf7787f5e01333c228b8e3f03c32f7295ea/src/graphics/native_guest_renderer.cpp#L153-L181).
- **Reordenação de cena, culling e materiais fixos.** O upstream conhece
  opacos, transparentes, bounds, esqueletos e render targets da engine.
  Ordenar draws PM4 por distância/material ou agrupar chamadas sem prova de
  equivalência pode alterar blending, queries e dependências de memória.
  [Ordenação de opacos upstream](https://github.com/mchughalex/skate3recomp/blob/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd/src/skate3_native_scene.cpp#L1107-L1111).
- **Câmera, bones, retenção de cena, sombras e novos efeitos.** Endereços e
  layouts da engine, correções visuais heurísticas e iluminação adicionada não
  pertencem ao renderer genérico. Qualquer perfil futuro exige identidade exata
  do título/revisão e trabalho separado; o fluxo upstream usa TU3, conforme seu
  README. Não importar seus hooks para a nossa preparação genérica.
- **White textures, frames antigos e fallback automático como otimização.**
  Não satisfazem a regra de fidelidade. Erro/dependência ausente permanece
  explícito; um replay não pode passar por ter usado outro caminho silencioso.

A correção mais recente do upstream também explicita `CopySource` nas texturas
usadas em readback. Serve como referência para contratos de criação/uso e
testes negativos, não como prova de um defeito local.
[Commit consultado](https://github.com/mchughalex/skate3recomp/commit/f6e0ae87fdfecbadb5c1e36c55d66a744187a3cd),
[flag na RHI](https://github.com/mchughalex/rexglue-skate3/blob/7eb0faf7787f5e01333c228b8e3f03c32f7295ea/include/rex/graphics/native_rhi.h#L114-L120).

## Execução futura e condições de aceite

Começar pela atribuição de custos e por uma única mudança pequena, escolhida
pelos resultados. Para investigar churn Vulkan, a sequência natural é medir
atualizações de textura, comparar reutilização e só depois avaliar allocator.
Evicção exige workload próprio acima do orçamento; workers ficam para quando
a conversão CPU restante justificar sua complexidade.

Usar o mesmo build Release, hardware/driver, configuração, cache e frames nos
dois lados. O [replay/A/B](../../../scripts/performance/native_replay_ab.py)
existente exige equivalência e repetibilidade; capturas de título ficam em
`.local/`, fora do Git. Separar CPU, GPU, waits, memória e P50/P95/P99; medir
custo de instrumentação e registrar também resultados sem ganho. Replay isola
o renderer e não demonstra FPS de gameplay nem elimina gargalos do título/PM4.

Os testes focados propostos em cada item devem demonstrar o contrato, incluindo
um controle que falhe quando ele for violado. WARP/lavapipe ajudam a verificar
correção, mas não substituem a medição na GPU alvo. Sessões visuais ou longas
continuam sujeitas à necessidade e à autorização previstas em
[CLAUDE.md](../../../CLAUDE.md); este estudo não as autoriza.
Preservar C7/C8, originais por versão, hashes e preparação reproduzível.

Para eventual adaptação de código, verificar os termos de cada arquivo e
preservar atribuições. O fork do SDK tem
[LICENSE BSD de três cláusulas](https://github.com/mchughalex/rexglue-skate3/blob/7eb0faf7787f5e01333c228b8e3f03c32f7295ea/LICENSE);
não foi encontrado arquivo de licença na árvore do skate3recomp consultada.
Isso não estende automaticamente a licença do SDK aos arquivos da aplicação.
As propostas aqui descrevem mecanismos; nenhum código, shader ou conteúdo
upstream foi incorporado.

Validação desta entrega: revisão das referências e dos caminhos locais,
`git diff --check` e conferência do escopo documental. Sem build, teste de jogo,
Computer Use, benchmark ou alteração de aceite funcional.
