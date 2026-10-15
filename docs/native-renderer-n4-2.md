# N4.2 — custo de pipelines e falsas esperas de textura

Trabalho local em 2026-10-15 sobre `41768ce`, preservando `aa3277b`, bindless,
espelho guest GPU, conversão compute e caches existentes. Nenhuma alteração no
SDK v0.10.0 `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, seus 74 patches,
plugin Xenos, interface pública de plugins ou seleção global do desktop.

## Causas medidas e mudanças

1. **Criação síncrona de PSOs sem persistência.** Na execução instrumentada
   original, 213 criações consumiram 71.327 ms de parede e 69.125 ms de CPU da
   thread de comandos. Tradução de shaders: 159 ms; texturas: 2.544 ms;
   uploads: 426 ms; resolves: 2.341 ms; fences: 23.416 ms. O tempo de draws
   (93.271 ms) inclui pipelines/texturas: esses contadores não são somáveis.
   A espera de fence mede tempo da CPU, não duração de execução GPU isolada.
2. **Sincronização de alternativas de textura que não leem memória.** O tradutor
   gera views signed/unsigned e dimensionais. A alternativa signed de RGB10
   exige conversão CPU se usada, mas o fetch unsigned a transforma em SRV nulo.
   A checagem anterior ao draw ignorava essa decisão e esperava um readback que
   nunca seria consumido. A execução aquecida anterior terminou com 45.336
   esperas `cpu_texture`, embora não tivesse cargas CPU. `TextureViewActive`
   agora compartilha exatamente a seleção usada por `BindTextures`; o último
   teste real registrou zero dessas esperas. Leituras CPU ativas continuam
   sincronizadas, inclusive o modo diagnóstico `native_texture_cpu`.

Native agora implementa o callback existente `InitializeShaderStorage`. Guarda
blobs locais do driver em `cache/native-d3d12-pso-v1/<title_id>/`, sem misturar
com o cache Xenos. A identidade inclui estado completo do PSO, root signature
serializada e bytes de todos os estágios. O hash escolhe o arquivo; comparação
integral da chave autoriza a leitura. Entradas truncadas são ignoradas. Blob
recusado pelo driver recompila o mesmo pipeline a partir dos shaders. Gravação
temporária e substituição evitam publicar arquivos parciais; falha de gravação
não impede renderização. Não há compilação assíncrona que pule draws.

O formato usa a ABI Windows do descritor, versionada pelo diretório, e blobs
opacos validados pelo driver. Não é cache portátil entre backends. Primeiros
encontros com pipelines continuam sujeitos a compilação: o cache não elimina
todos os hitches a frio.

`native_stats` permanece desligado por padrão. Quando ativado, separa tradução,
PSOs, CPU da thread/processo, uploads, texturas, resolves e fences.
`native_pso_dump` opcional salva somente os inputs do último PSO para reprodução
isolada; o caminho vazio não salva dados. Dumps e conteúdo proprietário ficam
somente em `.local`.

## Referências e experimento controlado

Lida a implementação real `external/rexglue-sdk/src/graphics/d3d12/pipeline_cache.cpp`:
Xenos persiste descrições e prepara pipelines no início; Native herdava o callback
vazio. Xenos não usa `CachedPSO` nessa revisão. O novo cache Native é implementação
própria, usando o blob do D3D12 e validando todos os inputs. A comparação de
memória, EDRAM e submissões com ShiftGlue/Pinyon/Xenia continua documentada na
[N4.1](native-renderer-n4-1.md); não foi incorporado código específico de Forza.

O PSO capturado em `n42-last-pso` levou 423 ms sem blob e 26 ms com o blob, em
processos separados com os mesmos DXBC/root/estado. Experimentos com root
signature de EDRAM estática e arrays limitados não removeram o custo (cerca
de 380–422 ms); essas alterações não foram adotadas.

No jogo, as primeiras 27 assinaturas de shaders/modificações/primitiva/MSAA são
a mesma sequência nas execuções fria e aquecida: soma dos tempos de PSO
registrados de 10.728 para 506 ms (valores individuais arredondados a ms).
Isso mede o caminho de pipelines, **não um ganho global de FPS**.

As amostras de pilha durante as pausas finais anteriores mostraram o command
processor aguardando comandos e o produtor guest em uma fila de trabalho,
não dentro de `CreateGraphicsPipelineState` nem aguardando uma fence. Não foi
isolada uma causa única dessa pausa guest. Ela não reapareceu na última execução,
com cache mais preenchido e a correção das falsas esperas; o efeito separado
dessas duas mudanças sobre essa pausa não foi certificado por A/B.

## Validação real e limite de aceite

Última execução: `20261015T234056Z-n4-bench-n42-waitfix`, mesmo executável AOT
diagnóstico existente, navegação e controles manuais. Chegou à condução do Viper,
com cores, texturas, minimapa e velocímetro visíveis. O mantenedor confirmou
aceleração, freio e direção. Exemplo preservado:
`.local/validation/native-n4/bench-n42-waitfix/17921076587714661/frame-122.ppm`,
convertido sem alteração de pixels para `n42-waitfix-latest.png`.

| Janela observada (hora local) | FPS | P50 / P95 / máximo (ms) |
|---|---:|---:|
| Cinemática, fim 20:42:30 | 10,46 | 99,16 / 132,57 / 148,83 |
| Cinemática, fim 20:42:35 | 11,72 | 83,68 / 115,96 / 116,58 |
| Condução, fim 20:43:13 | 5,34 | 150,35 / 535,51 / 718,64 |
| Condução, fim 20:43:18 | 5,44 | 164,27 / 217,59 / 666,11 |

Também houve hitches de novos pipelines: janela de 2,34 FPS e quadro de
2.116 ms na transição, e máximo de 3.050 ms na janela terminada às 20:43:08.
Portanto **não estão eliminadas todas as pausas de segundos**, nem certificado
desempenho estável a frio. Não comparar estas cenas como A/B com N4.1, B5 ou
Pinyon. Foi proposta uma dupla de execuções equivalentes aquecidas; o mantenedor
pediu **concluir com as evidências atuais**, e ela não foi realizada.

A execução avançou até 158.006 ms e terminou com **exit 78**, no comando
`EVENT_WRITE_ZPD` (`0x5B`) não implementado. Não foi timeout, nem saída limpa,
nem consulta de oclusão simulada. A implementação de oclusão não foi iniciada.
Resultado: recuperação funcional parcial comprovada, incluindo gameplay curto;
o aceite integral de desempenho contínuo permanece pendente.

## Testes e evidências

- Cache PSO: 17 asserções, incluindo blob real reutilizado no D3D12, identidade
  por conteúdo, mudança de estado/shader, chave adulterada e truncamento.
- Views inativas: 13 asserções, incluindo RGB10 signed/unsigned, dimensões e
  fetch inválido; preservada a classificação de conversão CPU ativa.
- Draw isolado Native, primeira execução e reutilização do cache: imagem igual
  à referência Xenos N4.1. SHA-256 dos PNGs:
  `038f08b74461738915a0c9c2ed8698e8a66f0c9bec9f45b6683f5324b04becda`.
  Conferência visual/numérica: esse snapshot produz somente dois tons neutros
  (0 e 24), não uma cena 3D completa. A igualdade testa esta reprodução restrita;
  a evidência de cores/texturas/HUD vem das capturas reais do jogo, não desse hash.
- Builds do plugin Forza e probes passaram, sem recompilar o AOT inteiro.
  As 4.005 asserções de conversão e 48 frames de scanout da N4.1 não foram
  repetidos: seus algoritmos não mudaram.

Dados agregados e comparação: `.local/validation/native-n4/n42-results.json`,
`n42-draw-comparison.png`, `n42_evidence.py`, `n42-replay/draw.png`.
`n42-scene-comparison.png` reúne a referência Xenos N4.1 e a condução Native
N4.2, identificadas como instantes diferentes, somente para inspeção visual.
Logs: `.local/logs/n42-{pipeline-test,replay-cold,replay-warm,final-game-build,final-focused-build}.log`.
Runs anteriores: `20261015T232123Z-n4-bench-n42-diag`,
`20261015T232940Z-n4-bench-n42-cache-cold`,
`20261015T233357Z-n4-bench-n42-cache-warm`.

Permanecem gargalos estruturais: `Flush` ainda acopla submissão, espera e
reciclagem, há sincronizações de pacotes e reconstrução de bindings por draw.
Não houve refatoração adicional, mudança de efeitos/MSAA, Computer Use,
push ou Release. Perfis, saves e Biblioteca foram preservados.
