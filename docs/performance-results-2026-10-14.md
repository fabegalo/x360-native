# Medições de performance — 2026-10-14

Política: **Performance-first, correctness-preserving**. Metodologia e comandos
em [performance.md](performance.md). Estes resultados descrevem os limites
observados; FPS de swaps não comprova fidelidade gráfica ou GPU sem erros.

## Ambiente e procedência

- Ryzen 7 5700X3D, 8 cores/16 processadores lógicos; RAM 42.856.640.512 bytes.
- RTX 4070, driver Windows 32.0.16.1742; Windows NT 10.0.26300.0.
- Display 1920×1080 a 279 Hz, plano de energia Equilibrado.
- D3D12/Xenos, vsync padrão ligado, escala 1×1, sem redução de fidelidade.
- LLVM 22.1.8/MSVC 14.44 via devshell; AOT RelWithDebInfo, strict FP.
- SDK pinado `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, patch 0070.
- Seed de perfil/save/cache copiada para cada execução. Cache do SO/driver não
  controlado. Jogos executados sequencialmente, sem build simultâneo.

Os AOT e DLLs foram congelados em `.local/benchmarks/binaries/baseline`. O Forza
foi compilado enquanto a integração independente do Guide tinha alterações
locais; não é um build limpo de HEAD. As opções do Guide não foram habilitadas
nestes diagnósticos. Comparações futuras precisam reutilizar os mesmos AOT e
identificar a DLL de runtime efetivamente medida. Os JSON registram SHA-256 de
todos os artefatos; HEAD/lock do checkout no lançamento não substituem essa
procedência. Não houve alteração dos originais ou publicação de recibos.

## Primeiras coletas

| Cenário | Janela | FPS / 1% low | Frame médio / P95 / P99 (ms) | CPU agregada/frame (ms) | Aceite |
|---|---:|---:|---:|---:|---|
| Dashboard Retail 17559 Home | 25–55 s | 60,003 / 54,487 | 16,666 / 18,036 / 18,209 | 35,810 | Home estável nas duas capturas |
| Castle Crashers | 30–60 s | 59,996 / 49,980 | 16,668 / 18,146 / 19,795 | 16,806 | Calibração: loading → menu |
| Forza Horizon | 145–175 s | 30,001 / 19,693 | 33,332 / 48,791 / 50,194 | 52,246 | Calibração: cinematic → transição preta |

Castle e Forza **não são referências definitivas** de menu/cena jogável nessas
coletas. O aquecimento dos cenários foi aumentado para 45 e 240 s respectivamente;
o aceite continua dependendo de capturas reais. Não inferir gameplay de um timer.
A primeira tentativa da dashboard abortou por leitura concorrente negada do CSV;
foi descartada. O runner foi corrigido para ler o arquivo após o encerramento.

CPU agregada inclui todas as threads e pode exceder o tempo de parede. Na
dashboard correspondeu a 13,429% da máquina; RAM peak amostrada 431.509.504 bytes,
private commit 963.866.624 bytes. Primeiro swap em 788,650 ms desde Runtime::Setup,
sem afirmar que esse é o tempo até a UI estar pronta.

Evidências estruturadas sanitizadas:
[Dashboard](../benchmarks/results/2026-10-14/dashboard-baseline.json),
[Castle/calibração](../benchmarks/results/2026-10-14/castle-calibration.json),
[Forza/calibração](../benchmarks/results/2026-10-14/forza-calibration.json).
CSV, logs, entrada temporizada e capturas originais permanecem sob
`.local/benchmarks/baseline-1` e `baseline-2`, com hashes nos exports.

## Gargalos observados e limites

1. A janela de Forza/calibração contou 401.585.493 dispatches em aproximadamente
   30 s. São chamadas observadas de FunctionDispatcher::Execute, não todas as
   funções AOT. Esse volume torna o overhead de instrumentação relevante.
2. Na dashboard houve 20.172.453 dispatches na janela. Em todas essas três
   janelas não houve tradução/criação de shader/pipeline nem misses dos caches
   de textura/pipeline instrumentados. Isso não mede o fast path consecutivo
   do pipeline nem prova que todo cache é ideal.
3. No conjunto da captura de Forza foram medidos 527 trabalhos de tradução
   (126.116 µs somados) e 481 criações de pipeline (7.562.317 µs somados).
   São durações de parede somadas, incluindo workers/falhas, não duração GPU
   nem atraso sequencial de inicialização. A carga se concentrou fora da janela.
4. Logs retidos: dashboard 3.639 erros/12.167 avisos; Castle 0 erros/3 avisos;
   Forza 57.515 erros/23 avisos. A rotação pode descartar mensagens anteriores.
   Há limitações já existentes de resolve/draw no Forza e exports não resolvidos
   da dashboard. Nenhum erro foi suprimido para melhorar números; não há alegação
   de execução graficamente correta ou livre de erros.
5. Tempo GPU, VRAM, PMU, caminho crítico de CPU e marco semântico de gameplay
   ainda indisponíveis; os JSON usam null e motivo. Não substituir por zero.

## Validação pertinente

O build nativo Forza com instrumentação passou. Os testes dos contadores passaram
15 asserções em dois casos: desabilitação, soma com writers concorrentes,
snapshot/reset concorrente sem perda e CSV/timer. Os redutores validam FPS,
cauda de 1%, percentis, CPU ausente e janela incompleta. A comparação verifica
identidade/configuração e revisão da cena; a exportação verifica retenção de
métricas e exclusão de caminhos privados. Replay do patch 0070 reproduziu os
seis hashes finais, SDK pinado preservado. Não foi repetida a suíte completa.

## Próximas prioridades

1. Quantificar e reduzir overhead dos contadores, preservando soma exata e
   sincronização. Comparar o mesmo runtime e cenas, sem atribuir ganho sintético
   a FPS comercial.
2. Finalizar referências de menu do Castle e gameplay do Forza com aquecimento
   conferido. Repetir A/B alternado antes de afirmar ganho de velocidade do jogo.
3. Adicionar timestamps GPU/DXGI sem espera síncrona para distinguir pacing e
   carga de GPU/CPU. Investigar os erros de resolve sem alterar fidelidade.
4. Medir custo do dispatcher/locks por Tracy antes de alterar sua estrutura;
   depois avaliar AOT LTO/PGO e inlining sob testes semânticos PPC.
5. Detalhar criação de PSO/caches persistentes e marcadores de carregamento,
   com identidade e invalidação corretas. Não otimizar misses inexistentes
   na janela quente só por hipótese.
