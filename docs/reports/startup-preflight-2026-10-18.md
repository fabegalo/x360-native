# Validação anterior ao processo — 18/10/2026

Escopo: reduzir a espera entre iniciar um título preparado e criar seu processo,
sobre `99a7764`, na `main` local. Abertura do launcher, implementação do catálogo,
renderer, SDK, perfis e saves não foram alterados. Os quatro títulos continuam
em `RelWithDebInfo` (`-O2 -DNDEBUG`). Os smokes autorizados usam cópias isoladas
dos dados de usuário e prazo de 30 segundos por processo.
Implementação: `508c9ea` (`perf(launch): parallelize protected preflight verification`).

## Método e fronteira do relógio

`x360n_launch_benchmark` percorre leitura de catálogo, validação de lançamento,
perfil/dependências, recibo final, identidade de EXE/DLLs e liberação dos handles.
Não cria processo guest. `total_ms` é esse caminho completo; `preflight_ms` é
somente o acumulado até `VerifyCatalogLaunch`, portanto não se deve confundi-los.
O relógio não mede clique, criação de janela, primeiro frame ou gameplay.

Baseline: implementações de `digest_batch.cpp` e `preparation.cpp` extraídas de
`99a7764`, ligadas ao mesmo harness/modelo, com read-ahead desativado pelo adaptador
exclusivo da comparação. Layout público de `DigestBatch` preservado. Diretório:
`.local/evidence/preflight-2026-10-18/baseline-source`. Controle adicional: os mesmos
dois fontes antigos com `-O2`, para separar otimização do compilador do algoritmo.
Os targets de benchmark são opcionais, `EXCLUDE_FROM_ALL`, sem execução automática.

Windows 10.0.26300, Ryzen 7 5700X3D, 8 núcleos/16 threads, LLVM 22.1.8 pinado,
CMake 3.31.6. Frontend em Debug, `-march=x86-64-v2 -O0`; a variante final aplica
`-O2` somente a `digest_batch.cpp` e `preparation.cpp`, preservando `_DEBUG`,
CRT Debug, símbolos CodeView e asserts. Demais fontes e presets inalterados.

Execuções consecutivas com cache do SO aquecido, sem purga, jogos ou builds
concorrentes. Cada filho do runner tem prazo de 60 s e lease exclusivo do ambiente.
As identidades SHA-256 dos três benchmarks, quatro recibos e seus artefatos são
conferidas antes/depois. I/O usa `GetProcessIoCounters`: bytes/operações lógicas
do processo, inclusive hits no cache do Windows; não representa tráfego físico
do SSD. CPU é tempo acumulado do processo; memória é pico de working set.

O acervo atual da 17559 inclui cópias de biblioteca de mais títulos preparados.
Seu baseline relê 21,94 GB, contra 13,95 GB no perfil inicial desta tarefa. Os
21,87 s históricos não constituem controle equivalente para esse acervo ampliado.
A preparação oficial precisou atualizar recibos/derivados invalidados pela mudança
do verificador compartilhado; o tempo desse rebuild não é vendido como startup
nem como nova otimização de build. Não houve mudança em originais ou SDK.

## Mudanças e provas preservadas

- Read-ahead do supervisor para inputs originais e cópias de biblioteca que os
  gates existentes inspecionarão. Não aprova título nem substitui nenhum gate.
- Até quatro workers para arquivos de pelo menos 1 MiB, com buffers independentes.
  Arquivos pequenos seguem seriais. A thread proprietária trata cancelamento;
  workers são unidos antes de publicar resultados ou liberar os arquivos.
- SHA-256 CNG permanece integral. Reutilização apenas na mesma transação, por
  identidade física, com exclusão de escrita/remoção e ancestrais protegidos.
  Nova operação relê bytes, mesmo com tamanho e timestamp preservados. Links e
  hosts sem exclusão equivalente conservam o caminho anterior.
- Recibos validam todas as identidades e todos os hashes. Inventários de globs
  compartilham enumeração apenas dentro da mesma chamada; padrões de sufixo
  comuns evitam regex equivalente. Outras expressões mantêm regex. Novas chamadas
  reenumeram fontes/DLLs e verificam marcadores, roles, SDK HEAD e âncoras.
- Os gates posteriores de perfil/recibo/identidade reutilizam os hashes protegidos
  da operação. Não há cache de aprovação persistente nem transferência de prova
  por timestamp entre launcher, supervisor e runtime.

## Resultados

Rodada final: `measured-4`, aquecimento B, seguido de B/A/A/B por título, duas
amostras medidas por variante. Média aritmética; amostra pequena, sem promessa
para outros discos, CPUs ou estados de cache. A aquisição do lease do runner
é medida separadamente do `total_ms`.

| Título | Antes, amostras (s) | Depois, amostras (s) | Média antes → depois | Redução |
|---|---:|---:|---:|---:|
| 17559 | 32,864 / 29,685 | 18,852 / 19,848 | 31,27 → 19,35 s | 38,13% |
| Charlie Murder | 9,281 / 9,250 | 7,741 / 7,738 | 9,27 → 7,74 s | 16,47% |
| Motocross | 10,907 / 10,907 | 7,984 / 7,978 | 10,91 → 7,98 s | 26,83% |
| Castle Crashers | 8,163 / 8,082 | 6,912 / 6,844 | 8,12 → 6,88 s | 15,32% |

### Tempos por etapa

Médias em milissegundos, antes → depois. Os hashes adiantados saem da etapa
`catalog_validation` e passam a `read_ahead`; deve-se comparar a soma delas.
O modelo do catálogo não foi editado. A variação da sua leitura está exposta,
incluindo o custo das rotinas compartilhadas de paths e o ruído de cache.

| Etapa | 17559 | Charlie | Moto | Castle |
|---|---:|---:|---:|---:|
| Leitura do catálogo | 4.719,5 → 3.695 | 3.822 → 3.693 | 3.928 → 3.652 | 3.853 → 3.662,5 |
| Read-ahead | 0 → 8.305 | 0 → 748,5 | 0 → 1.317 | 0 → 497 |
| Validação catálogo/recibos/inputs | 25.002 → 6.333,5 | 4.654,5 → 2.758 | 6.245 → 2.552,5 | 3.602 → 2.296 |
| Perfil da sessão | 961,5 → 560 | 335 → 204,5 | 325 → 188 | 293 → 171,5 |
| Dependências da sessão | 12 → 12 | 0 → 0 | 0 → 0 | 0 → 0 |
| Recibo/gate final | 317 → 183 | 338,5 → 217,5 | 322,5 → 188 | 291 → 165,5 |
| Identidade EXE/DLLs + liberar handles | 261 → 259 | 113 → 115,5 | 84,5 → 80 | 82,5 → 81,5 |

Zero indica duração inferior à resolução inteira do registro, não ausência do gate.
O lease do runner levou 0,30–5,44 ms (média 0,60 ms nas 20 execuções, incluindo
aquecimentos); não explica as esperas em segundos. Nenhum CMake, Ninja, codegen
ou compilador foi executado nesse caminho. CMake aparece como bytes dos inputs
do recibo e inventários de `CONFIGURE_DEPENDS`, que ainda são verificados.

### I/O, CPU e memória

Valores estáveis nas duas amostras de cada variante, exceto CPU/working set,
que são médias. GB decimal; MiB binário. `OtherOperationCount` inclui operações
de metadados/handles, não somente enumerações.

| Título | Arquivos SHA antes → depois | Bytes SHA (GB) | Bytes lidos (GB) | Operações de leitura | Outras operações |
|---|---:|---:|---:|---:|---:|
| 17559 | 14.119 → 14.161 | 21,9436 → 22,0492 | 21,9586 → 22,0642 | 50.585 → 50.752 | 191.287 → 209.212 |
| Charlie | 6.021 → 6.021 | 2,1126 → 2,1126 | 2,1203 → 2,1203 | 15.461 → 15.461 | 116.203 → 119.314 |
| Moto | 4.287 → 4.287 | 4,7731 → 4,7731 | 4,7806 → 4,7806 | 14.594 → 14.594 | 105.814 → 104.861 |
| Castle | 4.234 → 4.276 | 1,3054 → 1,4110 | 1,3122 → 1,4178 | 11.169 → 11.336 | 103.688 → 108.146 |

| Título | CPU acumulada antes → depois (s) | Pico de working set antes → depois (MiB) |
|---|---:|---:|
| 17559 | 28,851 → 28,773 | 60,76 → 73,07 |
| Charlie | 9,234 → 8,531 | 47,04 → 53,15 |
| Moto | 10,828 → 9,836 | 43,15 → 49,41 |
| Castle | 8,109 → 7,492 | 42,92 → 46,93 |

O read-ahead da 17559 autentica 19,215 GB; a validação seguinte autentica mais
2,834 GB. Nas etapas de perfil, recibo final e identidade EXE/DLLs há **zero bytes
SHA novos**: os handles continuam protegendo os resultados usados pelos gates.
Ainda se leem TOML/JSON e se conferem inventários/estado atual. Não há alegação
de eliminar a leitura obrigatória nem de reduzir I/O em todos os títulos.

Há custo especulativo: 17559 e Castle leem 42 arquivos/105.640.150 bytes adicionais
na antecipação de grupos que o caminho serial não chega a ler integralmente.
Esse custo é estável e está incluído nos tempos finais; não é uma leitura
que aprova arquivos inválidos. O ganho de parede não é vendido como redução dos
bytes totais. Os quatro buffers podem acrescentar 4 MiB durante o hash; os demais
custos vêm dos jobs, inventários e handles. Em disco lento os ganhos podem diferir.

### Perfil detalhado e custo indispensável

Uma execução separada da 17559 com `X360N_ACTION_TRACE` habilitado (`trace-final`)
levou 26,708 s. O log opcional abre/grava muitos registros e perturba o caminho;
esse número **não substitui** os benchmarks sem trace. O perfil registrou:

| Região | Chamadas | Soma de duração |
|---|---:|---:|
| `hash.stream` (ReadFile + CNG) | 14.161 | 28.936,5 ms |
| `receipt.inputs` | 10 | 7.377,3 ms |
| `receipt.artifacts` (EXE/DLLs) | 10 | 333,7 ms |
| `receipt.globs` | 10 | 456,9 ms |
| Read-ahead do lançamento | 1 | 12.630,9 ms |
| Validação do catálogo | 1 | 9.295,6 ms |

Essas regiões se sobrepõem e contêm sub-regiões: **não somar a coluna para obter
parede ou CPU**. `hash.stream` nos workers inclui leitura e SHA-256 e pode somar
mais que a duração total; o trace não separa tempo exclusivo de CNG de `ReadFile`.
Os contadores de I/O medem o volume/operações, não tempo de espera físico do SSD.

A maior parcela continua sendo ler/autenticar conteúdo, ferramentas, fontes e
produtos cobertos pelos recibos. Nenhuma aprovação por mtime/tamanho atende ao
requisito de detectar troca de bytes preservando ambos. CMake/codegen não são
executados no preflight; seus fingerprints e inventários não podem ser ignorados
sem alterar o contrato atual de prontidão. EXE/DLLs precisam ser autenticados,
mas seus hashes não são refeitos pelos gates seguintes enquanto protegidos.
Criar/liberar milhares de handles, ler os perfis e conferir diretórios também
tem custo; não foi removida a conferência de novos arquivos para escondê-lo.

Uma prova desta transação não é exportada como aprovação persistente para outro
processo. A criação do filho libera o lote como antes; o runtime conserva seus
gates próprios. Transferir essa confiança exigiria preservar a exclusão e vincular
as identidades ao carregamento real, fora do contrato existente. Sem alterar
launcher/SDK ou enfraquecer gates, não há promessa de startup instantâneo. A leitura
do catálogo continua em aproximadamente 3,7 s na variante final e foi mantida.

### Controles e resultados sem ganho

O primeiro passe, ainda sem `-O2` nos dois fontes, obteve 29,99 → 20,89 s na
17559; 9,41 → 8,71 s em Charlie; 10,89 → 8,69 s em Moto; 8,20 → 7,73 s em Castle
(`measured-2`). Paralelismo sozinho deixou ganhos pequenos nos dois jogos menores.

O controle antigo com `-O2`, em sua própria comparação alternada (`measured-3`),
teve 30,14 → 27,74 s na 17559, 9,63 → 8,12 s em Charlie, 10,85 → 9,42 s em Moto
e 8,15 → 6,78 s em Castle. A variante experimental que consultava `file_size`
de cada input antes de decidir o read-ahead piorou para 23,41 / 9,43 / 9,31 /
8,13 s. Na 17559, `OtherOperationCount` subiu de 209.212 no primeiro passe para
287.366 nessa tentativa. Ela foi descartada; a versão final não faz essas
consultas de classificação. Fontes e JSONs experimentais foram preservados.

## Regressões e smokes autorizados

Os filtros finais `[digest-batch],[preparation],[sha256],[avatar]` passaram:
23 casos/290 asserções. `[launch-preflight],[session]` passou: 14 casos/32.215
asserções. Total **37 casos/32.505 asserções**, sem suíte completa. Inclui SHA
de referência, 1/4/20 workers (limite efetivo de 4), duplicatas/ausências, cancelamento
e liberação de handles, exclusão de escrita/remoção, alteração com mesmo tamanho
e timestamp, links/junctions/caminhos Unicode longos, novos fontes/DLLs, marcador
de preparação incompleta, vínculos de catálogo e isolamento de versões/avatares.
Logs finais: `release-candidate-integrity.txt` e `release-candidate-launch.txt`.

Os quatro processos nativos foram executados sequencialmente com o diagnóstico
limitado e contexto da 17559. Não houve Computer Use, navegação ou teste visual.
Os smokes incluem carregamento adicional do contexto de dashboard/avatares para
os jogos, por isso seus tempos não substituem o comparativo do harness acima.

| Título | Preflight do smoke | Processo iniciado desde entrada do helper | Duração do filho | Resultado |
|---|---:|---:|---:|---|
| 17559 | 19,915 s | 20,424 s | 30,172 s | Swaps registrados; encerrado pelo prazo |
| Charlie | 8,895 s | 8,983 s | 2,126 s | Saiu com código 0, pediu dashboard; sem swaps |
| Moto | 9,840 s | 10,335 s | 30,200 s | Swaps registrados; encerrado pelo prazo |
| Castle | 8,458 s | 8,505 s | 30,172 s | Swaps registrados; encerrado pelo prazo |

CSV: 1.620 registros em 17559, 780 em Moto e 1.620 em Castle. O primeiro registro
ficou em 593,147 / 716,700 / 483,678 ms **desde `Runtime::Setup`**, respectivamente.
Não é tempo desde o clique nem certificação de imagem visível/primeiro frame;
não se soma esse valor ao relógio do preflight para alegar essa medição. Sem
comparação de FPS/gameplay. O código 1 nos três filhos corresponde ao encerramento
pelo Job Object no prazo, com `timed_out=true`, e não a uma falha de integridade.

Charlie chegou a HostLoader/RuntimeHost/NetCF, descarregou `netcfusermode.dll` e
emitiu `XamLoaderLaunchTitle: destination=<dashboard>`. O log também registra
`ResolvePath(RuntimeHost.xex) failed - device not found`. Não se atribui causalidade
exclusiva a essa mensagem nem se declara sucesso funcional pelo exit code 0.
A investigação funcional anterior continua válida como limite, sem alteração
de PTE, XAM, renderer ou SDK nesta tarefa.

Os seeds de usuário foram copiados e conferidos antes de executar. A auditoria
após os quatro smokes confirmou **1.132 arquivos originais de usuário com o mesmo
SHA-256** (`smoke/originals-preserved.txt`). Os binários/recibos dos benchmarks
também conservaram suas identidades durante a comparação (`comparison-preserved.txt`).

## Evidências e reprodução

Evidência privada em `.local/evidence/preflight-2026-10-18/`: JSONs por execução,
manifestos de identidade, ambiente, logs de regressão e smokes. Nenhum original,
perfil, save ou binário derivado entra neste relatório/repositório.

Runner somente de leitura:

```powershell
scripts/performance/benchmark-preflight.ps1 `
  -Executable .local/build/startup-performance/src/desktop/x360n_launch_benchmark.exe `
  -OutputDirectory .local/evidence/preflight-new-run -Samples 2
```

O diagnóstico que executa títulos é separado: `x360n_launch_smoke`, não registrado
em CTest. Requer diretórios distintos sob `.local/evidence`, dados de usuário
previamente copiados, contexto de dashboard validado e encerra o filho pelo Job
Object após 30 s. `launched=true` comprova criação do processo, não sucesso do
jogo. Falhas nativas e ausência de swaps permanecem registradas.
