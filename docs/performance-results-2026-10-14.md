# Medições de performance — 2026-10-14

Política: **Performance-first, correctness-preserving**. Metodologia e comandos
em [performance.md](performance.md). Estes resultados descrevem os limites
observados; FPS de swaps não comprova fidelidade gráfica ou GPU sem erros.

Continuação: [P2 — instrumentação OFF/ON em jogos reais](performance-p2-2026-10-14.md)
mede CPU/RAM externa com o mesmo SDK0072 dos dois lados. As pendências e números
abaixo descrevem esta rodada anterior; o microbenchmark não prova ganho de FPS.

## Ambiente e procedência

- Ryzen 7 5700X3D, 8 cores/16 processadores lógicos; RAM 42.856.640.512 bytes.
- RTX 4070, driver Windows 32.0.16.1742; Windows NT 10.0.26300.0.
- Display 1920×1080 a 279 Hz, plano de energia Equilibrado.
- D3D12/Xenos, vsync padrão ligado, escala 1×1, sem redução de fidelidade.
- LLVM 22.1.8/MSVC 14.44 via devshell; AOT RelWithDebInfo, strict FP.
- SDK pinado `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`; primeiras coletas
  com 0070 e comparação final de 0071 contra 0072.
- Seed de perfil/save/cache copiada para cada execução. Cache do SO/driver não
  controlado. Jogos executados sequencialmente, sem build simultâneo.

Nas primeiras coletas, AOT e DLLs foram congelados em `.local/benchmarks/binaries/baseline`. O Forza
foi compilado enquanto a integração independente do Guide tinha alterações
locais; não é um build limpo de HEAD. As opções do Guide não foram habilitadas
nestes diagnósticos. Comparações futuras precisam reutilizar os mesmos AOT e
identificar a DLL de runtime efetivamente medida. Os JSON registram SHA-256 de
todos os artefatos; HEAD/lock do checkout no lançamento não substituem essa
procedência. Os benchmarks não alteram originais nem publicam recibos.

Após a correção do Guide e CPU inicial, o baseline final foi congelado em
`.local/benchmarks/binaries/sdk71` a partir das três preparações aprovadas por
`dd0e9b6`. A variante `sdk72` reutiliza exatamente os mesmos AOT e DLLs de GPU,
mudando somente `rexruntimerd.dll` (0071: `1480364ce9c123cd9b5d44ae1109ef08306ee982d68ff53d162862e5d1a1ecc8`;
0072: `a1159404a7aac4c5ff0c0ff6ffd3dcea9f2cf72391fc1d21e2161c3088baa96f`).
O baseline foi copiado enquanto o commit final do Guide era concluído; os
binários efetivos são identificados pelos hashes, sem inferência a partir do HEAD.

## Primeiras coletas

| Cenário | Janela | FPS / 1% low | Frame médio / P95 / P99 (ms) | CPU agregada/frame (ms) | Aceite |
|---|---:|---:|---:|---:|---|
| Dashboard Retail 17559 Home | 25–55 s | 60,003 / 54,487 | 16,666 / 18,036 / 18,209 | 35,810 | Home estável nas duas capturas |
| Castle Crashers | 30–60 s | 59,996 / 49,980 | 16,668 / 18,146 / 19,795 | 16,806 | Calibração: loading → menu |
| Forza Horizon | 145–175 s | 30,001 / 19,693 | 33,332 / 48,791 / 50,194 | 52,246 | Calibração: cinematic → transição preta |
| Castle Crashers, menu | 75–105 s | 59,998 / 50,843 | 16,667 / 18,112 / 18,772 | 17,319 | Local Game estável nas duas capturas |
| Forza Horizon, nova calibração | 240–270 s | 29,903 / 15,394 | 33,442 / 48,844 / 49,705 | 55,463 | Rejeitado: PRESS START / attract movie |

As primeiras janelas de Castle e Forza não certificaram menu/cena jogável.
Com 75 s de aquecimento, Castle confirmou menu estável, 0 erros,
3 avisos e 0 swaps falhos. RAM amostrada 485.072.896 bytes, private commit
941.715.456 bytes; primeiro swap em 415,594 ms desde Runtime::Setup.

Forza com 240 s continuou no ciclo de apresentação: o Start não iniciou a sessão.
Aquecimento adicional sozinho não resolve. O roteiro foi ajustado para esperar
o foco, rotear para a janela filha do PID, preservar o hold após o foco e enviar
Start aos 30 s por 250 ms. Outra coleta com SDK0071 ainda ficou em PRESS START;
o roteiro final acrescenta confirmações de Start/Single Player, incluindo A aos
110 s. A calibração com essa confirmação chegou à cena jogável; foi separada
da comparação porque a entrada adicional ocorreu fora do roteiro inicial.
Os erros/avisos retidos não foram ocultados. Não inferir gameplay de um timer.
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
[Castle/menu](../benchmarks/results/2026-10-14/castle-baseline.json) e
[Forza/entrada não aceita](../benchmarks/results/2026-10-14/forza-input-calibration.json)
registram a segunda rodada.
CSV, logs, entrada temporizada e capturas originais permanecem sob
`.local/benchmarks/baseline-1`, `baseline-2` e `baseline-3`, com hashes nos exports.

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
   Há limitações já existentes de resolve/draw no Forza e chamadas a stubs XAM
   preexistentes da dashboard. Nenhum erro foi suprimido para melhorar números; não há alegação
   de execução graficamente correta ou livre de erros.
5. Tempo GPU, VRAM, PMU, caminho crítico de CPU e marco semântico de gameplay
   ainda indisponíveis; os JSON usam null e motivo. Não substituir por zero.

## Comparação nos títulos — primeiro par 0071/0072

Janelas de 30 s: Home aos 25 s, menu Castle aos 75 s e gameplay Forza aos
240 s. Mesmas seeds, configuração, coletores, originais, AOT e DLLs de GPU;
somente o runtime muda. Todos os pares passaram no comparador de procedência
e tiveram as duas capturas revisadas. Encerramento limitado e forçado pelo
runner, não retorno normal ao dashboard. Sem builds simultâneos.

| Cenário / runtime | FPS | 1% low | Frame médio / P95 / P99 (ms) | CPU agregada/frame (ms) |
|---|---:|---:|---:|---:|
| Dashboard / 0071 | 60,005 | 53,757 | 16,665 / 18,151 / 18,536 | 35,775 |
| Dashboard / 0072 | 60,000 | 53,498 | 16,667 / 18,282 / 18,565 | 36,131 |
| Castle / 0071 | 59,996 | 50,426 | 16,668 / 18,519 / 19,105 | 18,413 |
| Castle / 0072 | 60,002 | 50,203 | 16,666 / 18,151 / 19,070 | 17,953 |
| Forza / 0071 | 30,001 | 27,495 | 33,332 / 35,365 / 36,133 | 78,855 |
| Forza / 0072 | 29,798 | 17,249 | 33,559 / 35,559 / 36,367 | 84,232 |

Dashboard e Castle ficaram essencialmente no mesmo limite de cadência. A queda
de CPU do Castle (2,5%) em um par não basta para atribuir ganho à mudança.
No Forza, quatro frames da variante 0072 duraram 51,59/66,73/101,67/118,40 ms,
piorando o 1% low. Nenhum desses frames registrou criação de PSO ou stalls do
command buffer. Não foram removidos como outliers. A repetição focada abaixo
investiga a variabilidade; não há alegação de ganho de FPS neste primeiro par.

Forza usa a primeira cena dirigível, carro parado junto à pista, HUD com 2,5 MI
e tráfego ativo. Pequenas diferenças de posição/tráfego persistem; não é replay
determinístico nem benchmark de corrida completa. O minimapa preto e os erros
de resolve/draw continuam presentes nos dois lados. Na janela 0071 houve dois
PSOs (13.979 µs somados); 0072 não criou PSOs. A calibração anterior sem PSOs
não autorizava generalizar ausência de compilação para toda execução.

CPU média da máquina: Dashboard 13,42→13,55%, Castle 6,90→6,73%, Forza
14,79→15,69%. Picos de RAM amostrada: 417,09→414,47 MiB, 462,78→464,06 MiB,
1172,41→1175,87 MiB, respectivamente. CPU agregada não distingue o caminho
crítico nem autoriza concluir que a GPU ou uma thread específica é o gargalo.

JSON integrais de antes/depois e comparações:
[Dashboard antes](../benchmarks/results/2026-10-14/dashboard-final-before.json),
[depois](../benchmarks/results/2026-10-14/dashboard-final-after.json),
[comparação](../benchmarks/results/2026-10-14/dashboard-comparison.json);
[Castle antes](../benchmarks/results/2026-10-14/castle-final-before.json),
[depois](../benchmarks/results/2026-10-14/castle-final-after.json),
[comparação](../benchmarks/results/2026-10-14/castle-comparison.json);
[Forza antes](../benchmarks/results/2026-10-14/forza-final-before.json),
[depois](../benchmarks/results/2026-10-14/forza-final-after.json),
[comparação](../benchmarks/results/2026-10-14/forza-comparison.json).
Incluem CPU/RAM, primeiro swap, tempos somados de shaders/PSOs, contadores,
erros/avisos e métricas indisponíveis explícitas. CSVs/capturas/logs permanecem
em `.local/benchmarks/final-before` e `final-after`.

## Repetição Forza e auditoria da retomada

O segundo par já estava coletado localmente, mas faltava consolidá-lo. Mesmos
binários, seed, roteiro e janela de 240–270 s, novamente com o carro parado na
primeira cena dirigível. As quatro capturas foram reconferidas: HUD 2,5 MI,
diferenças pequenas de posição/tráfego e indicador SAVING em uma captura.
Isso não certifica save/load nem replay determinístico.

| Runtime | FPS | 1% low | Frame médio / P95 / P99 (ms) | CPU agregada/frame (ms) |
|---|---:|---:|---:|---:|
| 0071, repetição | 30,003 | 27,353 | 33,330 / 35,252 / 36,327 | 79,846 |
| 0072, repetição | 30,001 | 27,861 | 33,332 / 35,134 / 35,611 | 80,593 |

O 1% low da 0072 variou de 17,249 a 27,861 entre as duas coletas, enquanto a
0071 variou de 27,353 a 27,495. Não há ganho de FPS comprovado nem dados
suficientes para atribuir causalmente os hitches à alteração. CPU agregada
subiu 6,82% no primeiro par e 0,94% no segundo; não há ganho de CPU do jogo
demonstrado. Os resultados desfavoráveis permanecem no conjunto. Na repetição,
0072 contou dez criações de pipeline (596 µs somados); 0071, zero.

[Antes](../benchmarks/results/2026-10-14/forza-final-before2.json),
[depois](../benchmarks/results/2026-10-14/forza-final-after2.json) e
[comparação](../benchmarks/results/2026-10-14/forza-comparison2.json).

A retomada partiu de `d1c12c4`: patch 0070, runner, testes e otimização 0072 já
existiam. Alterações locais acrescentavam o roteiro Forza, descarte de último
registro sem newline, exclusão de calibração, exportação com campos permitidos e
redutor de evidência preservada. Foram mantidas e concluídas, sem reconstrução
do runtime. A auditoria corrigiu validação que pulava aquecimento/cauda, casts
de valores ausentes para zero, somas via double, arredondamento de bordas para
milissegundos inteiros, aceite de comparação sem métricas/executável e perda
do erro inicial ao reduzir novamente. O caso real `final-after2/forza_horizon`
agora conserva `Non-monotonic frame timestamps` de `result.initial.json`: a
causa era um fragmento final sem newline, descartado de forma explícita.

Oito CSVs reais foram reprocessados, mantendo os bytes originais e a procedência
da coleta; as quatro comparações passaram e os exports foram atualizados.
Backups dos resultados anteriores à retomada ficam em
`.local/validation/performance-recovery/prior`. A conferência independente usa
diferenças inteiras de microssegundos para FPS, cauda, percentis, CPU e totais,
além de hashes dos AOT/DLLs, módulos XEX originais e seeds em disco.
[Resultado da conferência](../benchmarks/results/2026-10-14/evidence-recovery-audit.json);
script local `.local/validation/performance-recovery/audit.ps1`, hash no JSON,
log `.local/logs/perf-recovery-evidence-audit.log`.
Nenhum código AOT, flag PPC, renderizador, módulo original, perfil ou save real
foi alterado neste incremento. Não houve nova sessão de jogo nem repetição do
ciclo Dashboard → Forza → Dashboard; sua evidência funcional anterior permanece
separada dos benchmarks com encerramento forçado.

## Validação pertinente

O candidato de layout de contadores foi medido com o fonte real do SDK compilado
em dois executáveis isolados, mesmas flags/dependências, mudando apenas slots
alinhados a 64 bytes. A DLL de suporte permaneceu idêntica; essa medição ainda
não corresponde à integração na DLL de produção. Ordem A/B/B/A, sete trials por
execução, um milhão de eventos por writer, totais exatos conferidos em cada trial.
ns/evento é parede total dividida pelo total de eventos das threads; mede custo
amortizado da carga, não latência individual de uma chamada concorrente.

| Contadores / writers | Mediana antes (ns/evento) | Depois | Amostras por lado |
|---|---:|---:|---:|
| Desligados / 1 | 2,03545 | 2,06210 | 14 |
| Desligados / 4 | 0,53911 | 0,53285 | 14 |
| Ligados / 1 | 2,57975 | 2,51785 | 14 |
| Ligados / 4 independentes | 6,95052 | 0,65908 | 14 |

Na carga concorrente habilitada, a redução mediana foi 90,518%; faixas
6,517–8,904 antes e 0,631–0,803 depois. É evidência de contenção entre linhas de
cache compartilhadas por contadores independentes. Os mesmos atômicos, ordens
de memória e snapshots são mantidos; writers do **mesmo** contador continuam
compartilhando o atômico. Não extrapolar esse resultado para ganho de FPS ou
execução normal com contadores desligados. JSON integral:
[experimento dos contadores](../benchmarks/results/2026-10-14/counter-candidate.json).

O mesmo candidato passou 15 asserções em dois casos, incluindo snapshots durante
escritas concorrentes, sem perda de eventos. Log local:
`.local/logs/perf-counter-candidate-tests.log`.

Após o commit `dd0e9b6` e o ciclo final do Guide, o candidato foi integrado
como patch 0072, sem mudar o SDK pinado. A/B dos jogos registrado abaixo.

## Comparação das DLLs reais (0071 → 0072)

Mesmo `counter_probe.exe`, mesmo TracyClient e mesmas opções; apenas a DLL
`rexruntimerd.dll` muda. Ordem A/B/B/A, sete repetições por execução e 14 por
variante/caso. Os testes com a DLL otimizada passaram 15 asserções em dois casos,
incluindo snapshots concorrentes sem perda. Log:
`.local/logs/perf72-dll-tests.log`. O build incremental compilou os dois arquivos
alterados e relinkou o runtime; não usou a DLL do experimento de fontes.

| Contadores / writers | Antes (ns/evento, mediana) | Depois | Variação |
|---|---:|---:|---:|
| Desligados / 1 | 2,37635 | 2,36425 | −0,51% |
| Desligados / 4 | 0,63274 | 0,61055 | −3,51% |
| Ligados / 1 | 2,59000 | 2,55150 | −1,49% |
| Ligados / 4 independentes | 11,99665 | 0,65208 | **−94,56%** |

Na carga concorrente habilitada, todas as amostras depois (0,643–0,665) ficaram
abaixo das anteriores (11,532–12,484). As variações pequenas dos outros casos
não fundamentam alegação de ganho. São nanossegundos amortizados por evento
(tempo total / total de eventos), não latência de uma chamada isolada.
O resultado demonstra redução do custo da instrumentação habilitada,
sem alegar ganho de FPS ou da execução normal com contadores desligados.
Dados integrais e hashes:
[comparação das DLLs](../benchmarks/results/2026-10-14/counter-dll-comparison.json).

O build nativo Forza com instrumentação passou. Os testes dos contadores passaram
15 asserções em dois casos: desabilitação, soma com writers concorrentes,
snapshot/reset concorrente sem perda e CSV/timer. Os redutores validam FPS,
cauda de 1%, percentis, CPU ausente e janela incompleta. A comparação verifica
identidade/configuração e revisão da cena; a exportação verifica retenção de
métricas e exclusão de caminhos privados. Replay do patch 0070 reproduziu os
seis hashes finais, SDK pinado preservado. Não foi repetida a suíte completa.

## Verificação nova das DLLs na retomada

O ensaio real dos contadores foi executado novamente em ordem A/B/B/A, usando
os mesmos executáveis e DLLs congelados e sete trials por execução (14 por lado).
Hardware/driver/plano de energia foram consultados novamente e correspondem ao
ambiente acima. Sem jogos ou builds simultâneos durante os trials.

| Contadores / writers | Antes (ns/evento, mediana) | Depois | Variação |
|---|---:|---:|---:|
| Desligados / 1 | 2,38585 | 2,38300 | −0,12% |
| Desligados / 4 | 0,62368 | 0,61566 | −1,28% |
| Ligados / 1 | 2,52190 | 2,57580 | +2,14% |
| Ligados / 4 independentes | 12,18065 | 0,65928 | **−94,59%** |

O único gargalo isolado causalmente é a contenção entre contadores independentes
na instrumentação habilitada. Os intervalos desse caso continuam separados:
11,103–13,142 antes e 0,622–0,831 depois. Os demais casos têm dispersão sobreposta.
Não se atribui esse ganho à execução normal desabilitada nem aos jogos.
[Dados completos](../benchmarks/results/2026-10-14/counter-recovery-comparison.json).

Testes focados: 4/4 `x360n.benchmark-*` pelo CTest (incluindo o novo teste de
redução repetida); testes nativos dos contadores passaram 15 asserções em dois
casos por DLL, 0071 e 0072. Os 72 patches aplicados e o pin foram conferidos,
sem editar o submódulo. Logs: `.local/logs/perf-recovery-ctest.log`,
`perf-recovery-sdk71-tests.log`, `perf-recovery-sdk72-tests.log` e
`perf-recovery-patch-check.log`. Configure via devshell passou; nenhum jogo foi
recompilado. A retomada não comprova o término do prepare anterior registrado
em `perf72-forza-prepare.log`, que acaba em `4-build`; usa os artefatos congelados
com hashes verificados, suficientes para este incremento. O log de compilação
parou em 71/376 e o diretório padrão do Forza está sem executável; benchmarks
desse título devem usar `-BinaryDirectory .local/benchmarks/binaries/sdk72/forza_horizon`
(ou sdk71). A preparação normal/distribuível não está aprovada por este ensaio.

## Próximas prioridades

1. Quantificar o custo total de instrumentar jogos contra execução desabilitada
   com uma fonte externa de frame timing. O ensaio dos contadores não mede o
   custo completo do CSV/GetProcessTimes nem comprova overhead zero.
2. Referências Home/Castle menu/Forza parado estão disponíveis. Mais repetições
   e controle do estado seriam necessários para atribuir mudanças de cauda
   ou CPU a uma otimização; não há motivo para alegar ganho de FPS agora.
3. Adicionar timestamps GPU/DXGI sem espera síncrona para distinguir pacing e
   carga de GPU/CPU. Investigar os erros de resolve sem alterar fidelidade.
4. Medir custo do dispatcher/locks por Tracy antes de alterar sua estrutura;
   depois avaliar AOT LTO/PGO e inlining sob testes semânticos PPC.
5. Detalhar criação de PSO/caches persistentes e marcadores de carregamento,
   com identidade e invalidação corretas. Não otimizar misses inexistentes
   na janela quente só por hipótese.
