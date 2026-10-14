# P3 — perfil real e consulta de registradores GPU

Estado: validação em andamento; não interpretar resultados parciais como aceite.
Continuação de `feed952`, preservando a P2 e o SDK fixado em `f5337cdc`.

## Situação do aceite

P3 **não concluída**. O primeiro par aceito apresenta redução de CPU, mas pior
frame pacing. A repetição não encerra essa investigação. Ainda faltam equivalência
do ambiente nas repetições, aprovação do pacing e validação pós-mudança de
controles, áudio e Dashboard → Forza → Dashboard. O executável na preparação
normal do Forza está ausente após uma preparação anterior incompleta; os
benchmarks usam cópias congeladas, sem substituir o executável nem produzir
recibos artificiais. O ciclo aprovado na P2 não certifica automaticamente a P3.

| Captura | CPU ms/s | FPS | 1% low | P95/P99 ms | Observação |
|---|---:|---:|---:|---:|---|
| before-01 | 2431,75 | 29,986 | 26,536 | 35,190 / 35,946 | Primeiro par aceito |
| after-01 | 2335,26 | 29,867 | 19,164 | 35,690 / 37,050 | CPU −3,97%; pacing pior |
| after-03 | 2266,82 | 29,884 | 19,726 | 35,650 / 36,700 | Repetição jogável, sem par aceito |
| before-02 | 2475,18 | 29,969 | 25,511 | 35,171 / 36,007 | Segundo par recusado pelo comparador |

`pair-02.json` preserva `comparable=false`: a coleta anterior registrou falha de
consulta CIM e a nova conseguiu identificar o hardware. A execução before-02
também ocorreu fora do sandbox, diferente do contexto das anteriores; não se
normalizaram metadados retrospectivamente para forçar aceite. O sidecar WDDM
desse run terminou com timeout de consulta, registrado no JSON. Seus dados são
parciais. `after-02` não alcançou a cena jogável e foi excluído, com evidência
preservada. Não calcular ganho agregado juntando essas execuções.

As três coletas completas de WDDM before-01/after-01/after-03 mostram utilização
média amostrada do engine 3D de 34,19% / 28,60% / 28,00% e pico de memória dedicada
de 1.128.079.360 / 1.134.747.648 / 1.133.633.536 bytes. Não constituem comprovação
de redução causal do trabalho GPU. `summarize-benchmark-gpu.ps1` preserva engines
e adapters separados e exclui consultas que terminam fora da janela.

Os tempos até primeiro swap variam de 1,726 a 9,031 segundos; não há melhoria de
carregamento comprovada, nem marcador semântico de cena pronta. Na after-03,
seis criações de pipeline consumiram 244.856 us na janela. Nenhum frame foi
removido dos percentis para esconder esses picos. Cache de driver e tráfego não
foram controlados por replay. GPU ms/frame permanece indisponível.

Erros gráficos existentes não foram silenciados: before-01 contém 141.451
erros nos logs retidos e after-03 contém
175.714. As amostras inspecionadas incluem `Resolve region is empty`,
`PM4_DRAW_INDX... Failed in backend` e fetch constant 90 inválida, já registrados
na validação anterior do Forza. Zero falhas de swap não significa zero erros
de renderização. A origem e a extensão desses defeitos não estão resolvidas por
esta otimização de metadata; não há aceite de fidelidade gráfica completa.

Regressão focada `p3-castle-after-01`: menu original animado antes/depois,
60,002 FPS, 1% low 49,869, P95 18,117 ms, P99 19,909 ms, CPU 1072,66 ms/s,
RAM 482.578.432 bytes, zero erros/críticos e zero falhas de swap. O teste passou
quanto a iniciar, aceitar Start e renderizar o menu com a DLL candidata.
Não é comparação causal de desempenho com a P2, teste de gameplay, áudio nem
ciclo pela dashboard. O coletor de threads distribuído compilou via devshell;
seu formato final não foi usado para substituir a captura diagnóstica histórica.

## Investigação

A captura `p3-profile-01/forza_horizon` reutilizou o cenário e os binários SDK72
congelados da P2, com contadores OFF. O profiler local amostrou o RIP de 82 threads
por 20 segundos na cena dirigível (início aos 240 s), sem erros de contexto.
As threads foram retomadas imediatamente depois de ler CONTEXT_CONTROL; símbolos
foram resolvidos depois da amostragem. Esses são samples de parede, incluindo
esperas, não amostras ETW ponderadas por CPU nem stacks completos. O intervalo fixo
de 11 ms pode sofrer aliasing. Esta execução é diagnóstica e está excluída dos
benchmarks, pois suspender threads interfere no programa.

A thread guest 35296 acumulou 19.421,875 ms de CPU. As funções AOT `829F04A8` e
`823E91F0` concentraram 594 e 371 samples. A inspeção do código gerado identifica
um laço que verifica progresso gráfico; a primeira contém as instruções originais
`cctpl`, `db16cyc` e `cctpm`, hoje sem operação host. Não foram alteradas espera,
semântica PPC, sincronização nem funções específicas do jogo.

A thread de comandos 1680 acumulou 13.546,875 ms de CPU. `GetRegisterInfo` teve
131 samples; `CommandProcessor::WriteRegister`, 115; a especialização D3D12, 99.
A consulta ocorre em cada escrita, inclusive com logging debug desligado, e usava
um switch com 3.434 entradas. Essa foi a oportunidade genérica e de baixo risco
selecionada. Volume de dispatches não foi tratado como prova de custo de dispatch.
O perfil identifica consumo de CPU e uma oportunidade; não mede sozinho o caminho
crítico de cada frame nem prova que o limite próximo de 30 FPS é um gargalo.

## Mudança

Patch `0073-index-gpu-register-metadata.patch`: tabela constante indexada, gerada
pela mesma `register_table.inc`, com nome nulo nas lacunas e verificação de limite.
Sem alocação, lock ou construção dinâmica por frame. Tipos, nomes, ponteiros
estáveis por índice e null para desconhecidos são preservados. Nenhum draw,
resolve, aviso, barreira, upload ou sincronização foi removido. Recursos originais
de dashboards não fazem parte dessa tabela host genérica.

O build da candidata compilou somente `register_file.cpp` e relinkou o plugin
`rexgpu-xenos`. O probe verificou 65.540 índices (0..65535 e quatro valores grandes)
em ambas as implementações, incluindo todos os 3.434 registradores conhecidos.
O checksum do microbenchmark é idêntico. Os tempos sintéticos são publicados
separadamente e não substituem as medições comerciais.

## Coleta e limites

O runner, cenário, seed de perfis/saves/cache, executável AOT e DLL de runtime são
os mesmos nos pares. Só muda `rexgpu-xenosrd.dll`. A GPU baseline veio do alvo
Forza RelWithDebInfo existente após conferir patches/build antes da mudança;
a candidata foi compilada na mesma árvore. Os originais e os artefatos congelados
da P2 foram preservados. Cada captura registra hashes. A procedência AOT/runtime
continua sendo a dirty source herdada da P2; HEAD do lançamento não a substitui.

WPR foi testado dentro e fora do sandbox. O token real não é administrador nem
Performance Log Users. `wpr -start CPU -filemode` falhou com `0xc5585011`,
“Failed to enable the policy to profile system performance.” Nenhuma permissão,
grupo ou serviço foi modificado. Frames OFF continuam indisponíveis; não foram
inventados FPS, 1% low ou percentis OFF. A captura ON mede cadência de XE_SWAP,
sem alegação de scanout. Os detalhes estão em `etw-limitation.json`.

O sidecar `benchmark-gpu.ps1` consulta GPU Performance Counters por PID e adapter.
Registra DedicatedUsage/SharedUsage/TotalCommitted e utilização por engine, com
intervalos e duração das consultas. São contadores WDDM amostrados: memória
dedicada não certifica residência física instantânea, e utilização não equivale
a GPU ms/frame. Queries sequenciais recebem atualização assíncrona do Windows.
Tempo GPU por frame continua null. O coletor funciona sem elevar o token Windows;
foi executado fora do isolamento de processos dos comandos para enxergar o PID.

No primeiro par, CPU caiu 3,97%, mas o 1% low piorou. O pior frame da candidata,
131,693 ms, coincide com 114.696 us de criação de pipeline e duas traduções de
shader; a baseline teve uma criação de 83 us. O dado é mantido, não removido da
comparação. As sementes de cache são iguais, mas tráfego e simulação não são um
replay determinístico. Ainda não há aceite de melhoria global de frame pacing.

## Reprodução

Os diretórios locais de binários são `p3-before/forza_horizon` e
`p3-after/forza_horizon`, dentro de `.local/benchmarks/binaries`.

```powershell
pwsh -NoProfile -File scripts/benchmark-runtime.ps1 -Title forza_horizon -Label nova-p3-before -BinaryDirectory .local/benchmarks/binaries/p3-before/forza_horizon -Instrumentation On
# Em outro processo local, enquanto o runner controla o jogo; usar seu PID exato:
pwsh -NoProfile -File scripts/benchmark-gpu.ps1 -ProcessId <PID> -Output <gpu.json>
# Depois do encerramento, repetir com p3-after e um label novo; revisar as cenas.
pwsh -NoProfile -File scripts/compare-benchmarks.ps1 -Before <run-before> -After <run-after> -ChangedArtifacts rexgpu-xenosrd.dll -Output <comparacao.json>
```

Compilação do probe sem recompilar jogos, via toolchain fixada:

```powershell
scripts/devshell.cmd clang++ -std=c++23 -O2 -DNDEBUG -Iexternal/rexglue-sdk/include -Iexternal/rexglue-sdk/thirdparty/simde tests/performance/register_info_probe.cpp external/rexglue-sdk/src/graphics/register_file.cpp -o .local/benchmarks/p3-tools/register-after.exe
.local/benchmarks/p3-tools/register-after.exe
```

Para a versão anterior, usar a cópia preservada de `register_file.before.cpp`.
O alvo CMake explícito `x360n_register_info_probe` também compila o probe com a
fonte SDK corrente. Não faz parte dos benchmarks automáticos.

Evidência pública em `benchmarks/results/2026-10-14/p3`; logs, PNGs, CSVs e dados
privados continuam em `.local/benchmarks/p3-*`. Sem push ou Release.
