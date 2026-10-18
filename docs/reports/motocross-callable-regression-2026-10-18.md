# Regressão de entrada chamável do Motocross — 18/10/2026

Escopo autorizado: investigar/corrigir a regressão de Motocross Madness, sem
hacks por jogo, validar apenas esse título e testes sintéticos relevantes,
fazer commit local, sem alterar o renderer.

## Evidência e causa

A execução anterior `20261018T022130Z-session-3-motocross_madness` durou
84.655 ms, saiu com código 0 e não registrou chamadas ausentes. EXE
`47b2f2f2a6b4e804ec3e5ac554ec3fa107e4f81e6907141437e757aff1cbabbc`.
É a referência curta de inicialização/retorno, não uma certificação de gameplay.

A execução posterior `20261018T043400Z-session-3-motocross_madness` terminou
em 2.545 ms, código `0xC0000409`, com `LR=82A8FE80 CTR=82B36410` e chamada
inválida/não registrada. EXE
`c808511aedc58ba78b487cbecf71672fb65b63d36418c963d5ce674f3109cd1b`.
Ambas precedem a correção de foco `a9cb5f4`. Logs completos preservados em
`.local/evidence/runs/`.

O patch 0090 reconheceu o despacho por byte em `0x82B36390`, mas enumerou
todos os 256 valores possíveis sem aproveitar o limite existente. O código
original compara o índice sem sinal com 41 e retorna quando ele é maior.
Portanto há 42 slots comprovados, não uma tabela de tamanho desconhecido.
A função usa os bytes originais em `0x82104860` como offsets não escalados
relativos a `0x82B363CC`.

A enumeração excessiva incorporou `0x82B36410` ao switch como label local.
Esse endereço é o início de um comparador independente, sem frame/PDATA,
invocado por ponteiro na rotina de ordenação. Como seu corpo já pertencia
ao switch anterior, GapFill não criou sua entrada e o registro gerado a omitiu.
Ter o corpo emitido e nenhum label ausente não assegura que os callbacks
necessários continuem registrados.

## Correção compartilhada

Patch `0092-guarded-byte-dispatch-domain.patch`, ligado ao SDK fixado pelo lock.
Sem endereço, hash, nome de jogo ou tabela manual no algoritmo.

O reconhecedor existente de `lbzx`/rotate-mask/add/`mtctr`/`bctr` agora refina o
domínio quando consegue provar uma sequência local:

- comparação sem sinal de 32 bits no mesmo registrador usado como índice;
- `bgt`/`bgtlr` no mesmo campo CR, sem link ou teste de CTR;
- entrada linear até a comparação, sem desvio que a contorne;
- construção constante da base da tabela, sem sobrescrever o índice;
- todos os bytes da extensão comprovada disponíveis na seção original.

Somente os valores dos slots comprovados alimentam a transformação PPC já
existente. Sem essa prova, permanece o domínio finito anterior; não se usa
comprimento padrão nem parada por aparência de código. O despacho continua
usando o CTR calculado em runtime, com resolução indireta para destinos externos.
A leitura de dados e a restrição acontecem no codegen, sem polling ou trabalho
novo no runtime. Não há alegação de ganho de performance de jogo.

O teste sintético reproduz um switch seguido de um callback sem PDATA/BL.
O produtor de produção anterior, guardado antes do rebuild, omite a entrada
`0x82010040`; o código corrigido a registra. O teste cobre também CR incorreto,
registrador diferente, ramo invertido, entrada que pula a comparação e tabela
inacessível. Esses casos não recebem um limite especulativo.

## Verificação e preservação

- Testes focados de despacho: quatro casos / 234 asserções aprovados.
- Conjunto relacionado de descoberta/GapFill/despacho: nove casos / 354
  asserções aprovados; nenhuma suíte completa nem outro jogo executado.
- Execução do despacho anterior sem limite conhecido: 3.072 chamadas nativas
  aprovadas, incluindo mutação dos bytes após o AOT.
- Probe novo compila o C++ **e o registro gerado** do fixture limitado: verifica
  limite, chamada do callback pelo dispatcher e fallback de CTR após alterar
  um byte da tabela para apontar ao callback externo.
  Foram aprovadas 18 chamadas nativas pelo código e registro reais do fixture.
- Replay do patch sobre a cópia anterior reproduz o arquivo do SDK byte a byte:
  SHA-256 `583f1a89d0a62f23fbc5549db2808541ad05f894965292c38c10ed75b81a3c0d`.

Evidência específica em `.local/evidence/motocross-regression-2026-10-18/`:
artefatos/recibo/produtor anteriores, fontes geradas relevantes, hashes dos
arquivos gerados e dos dados do usuário, backup de `.local/user/motocross_madness`
e replay isolado. Logs de comandos com prefixo `moto-` ficam em
`.local/evidence/aot-ctr-dispatch-2026-10-17/`, pelo harness limitado já existente.
Falhas iniciais dos fixtures também foram preservadas, separadas das execuções
do jogo. Nenhum input, perfil, save ou patch anterior foi removido.

## Preparação de produção

Motocross foi preparado pelo driver oficial `scripts/build-title.ps1`, com quatro
jobs, sem editar recibos ou pular os gates. Duração total 1.042 s; ferramenta
234 s, codegen 66 s e compilação/link nativos 555 s. As etapas/condições diferem
da referência anterior; esses tempos não são um benchmark de performance.

- EXE: `9cf14ae517580c2790837b450853efd0a2c410c0266a11e849c951a00cab4099`.
- Recibo: `72892e455081e92762992f9108ddf8e004b5dac665a139833a365ab508e89a1b`.
- 3.624 inputs, seis artefatos e três arquivos do produtor verificados, globs
  correspondentes, sem marcador de preparação incompleta.
- 325 unidades C++ / 70.302 funções / 161 despachos por CTR auditados, zero labels
  ausentes. O módulo de entrada passou de 51.147 para 51.148 registros, adicionou
  somente `0x82B36410` e não removeu nenhuma entrada anterior.
- O switch problemático passou de 35 destinos potenciais para nove destinos
  presentes na tabela original limitada. O callback não aparece mais como caso.

A alteração do lock invalida também o recibo anterior da dashboard. O launcher
revalida esse recibo ao fornecer o host original de Guide/avatares ao Motocross;
portanto o build 17559 é uma dependência necessária para a entrega pelo launcher.
Ele é repreparado pelo mesmo driver, sem executar uma sessão de dashboard nem
regressão de outro jogo. Módulos e recursos continuam exclusivamente da 17559.

A primeira preparação dessa dependência compilou/linkou todos os targets, mas
o monitor atingiu 1.500 s durante a coleta/conferência final: exit 124, sem recibo
novo e com marcador de incompleto preservado. Logs em `dashboard-attempt-1/` e
`moto-prerequisite-17559-prepare.*`. A retomada usa o driver oficial, oito jobs
e limite de 3.000 s; não há publicação manual ou afrouxamento das verificações.
A geração repetida manteve os 428 arquivos do manifest principal idênticos.

A segunda tentativa compilou/linkou em 331 s, mas encerrou com exit 1 após
668,7 s: os scripts de preparação haviam sido alterados por outra atividade
durante a execução. O gate identificou a troca e não publicou recibo.
O outro chat também executava uma medição que alterava temporariamente um
comentário do Motocross com timestamp preservado, invalidando seu recibo.
Após coordenação autorizada pelo usuário, essa medição foi encerrada pelo
próprio chat, o fonte foi restaurado byte a byte e o driver foi estabilizado.
As retomadas oficiais usam logs `moto-prerequisite-17559-stable` e
`moto-regression-prepare-stable`. Como o estado incremental autenticado ainda
não existia, o driver exigiu rebuild seguro; nenhum recibo foi editado ou aceito
manualmente. As mudanças de infraestrutura pertencem ao trabalho paralelo e
não integram o patch de codegen desta correção.

Nessas retomadas, os dois wrappers Windows PowerShell do Ninja permaneceram
sem filhos ativos e sem progresso após o link de `rexgluerd.exe`. Somente as
árvores iniciadas por esta tarefa foram encerradas, após conferir comando,
origem e criação por CIM; snapshots `*-stable-wrapper-stall-tree.json` preservam
a identificação. As tentativas terminaram com exit 124 após 744,9 s (17559) e
697,4 s (Motocross), por interrupção explícita, não pelo watchdog (`timeout=false`).
Não houve recibo novo. A hipótese de interferência do ambiente foi isolada
retomando o mesmo driver fora do sandbox, com PowerShell não interativo,
oito jobs e o mesmo limite de 3.000 s, nos logs `*-host`.

As retomadas fora do sandbox terminaram com exit 0: 17559 em 890,5 s (compilação
nativa 317 s) e Motocross em 990,6 s (nativa 402 s). Ambas as ferramentas levaram
209 s; o codegen repetido do Motocross manteve 666 arquivos idênticos. Os tempos
incluem preparações simultâneas e infraestrutura alterada pelo trabalho paralelo;
não constituem comparação de performance equivalente.

Artefatos finais, publicados exclusivamente pelo driver:

| Título | Inputs / artefatos / produtor | SHA-256 do recibo |
| --- | --- | --- |
| Motocross | 4.170 / 6 / 3 | `38fc899905606208065e0037758012ffb71ac8e977451dda3c2ecb7c0ba39cfc` |
| 17559 | 4.012 / 13 / 3 | `ccd42a028079153048df79317484b5180b9d4e51133c73e8ca376a93e438f0d8` |

EXE final do Motocross:
`bed6d84672d077ef03ead10f95c676dee29441ea39504d1fbe30140d7e449c06`.
Ambos os marcadores de incompleto foram removidos pelo driver após a publicação.
O supervisor aceitou a preparação para executar o teste real. Metadados em
`receipts-final.json`; os recibos contêm também o checkpoint autenticado novo.

## Execução curta final

Por solicitação posterior do usuário para acelerar e apenas conferir a execução,
o teste final foi reduzido para 20 s, sem probe de Guide nem sessão de outro jogo.
O código e os testes da correção foram commitados localmente em `e3adcd0`.

A primeira execução usou dados temporários sem perfil. O Motocross superou a
falha original, e `avatar-service: READY` foi registrado. Após 8.582 ms, porém,
o fluxo original de login caiu com `LR=90108A40 CTR=90116C88`, exit `0xC0000409`.
O módulo é `system/signin.xex` 17559, registrado com 1.071 funções; a enumeração
encontrou zero perfis. Houve uma linha crítica e uma chamada não registrada.
Essa execução **falhou** e não foi aceita apenas pelo marco READY.
Evidência: `runs/20261018T060910Z-moto-regression-startup/` e `smoke-result.json`.

O lançamento anterior pelo My Games fornecia `x360n_profile_root` e um perfil
ativo. Para distinguir esse caminho do fluxo sem perfil, uma única confirmação
de 20 s usa cópia dos dados do perfil local, sem cache e sem alterar o original,
mais as preferências de idioma/país da referência. Não há novo rebuild, perfil
inventado, alteração de módulos originais ou contorno de chamada ausente.

Essa confirmação **não abriu o jogo**: o preflight recusou
`cmake/sdk/x360n_sdk_backports.cmake`, alterado por atividade paralela às 03:10:30
depois da publicação do recibo. Log `moto-live-profile-startup20.log`, exit 1.
Nenhuma verificação foi desabilitada; não se atribui sucesso a essa tentativa.
O usuário confirmou a diferença de perfil e solicitou encerrar a tarefa.
Não houve nova sessão nem novo rebuild após essa instrução. A janela compartilhada
foi liberada, e os arquivos do trabalho paralelo foram preservados.

Resultado entregue: causa original corrigida genericamente, testes focados e
builds aprovados; execução real passou da queda original e chegou a READY.
O fluxo sem perfil revelou uma chamada ausente em signin 17559. Uma execução
completa de 20 s com perfil ativo, o caminho My Games atual e gameplay não foram
certificados nesta tarefa. Os recibos publicados refletem o snapshot do build;
alterações posteriores de infraestrutura exigem sua revalidação normal.

A conferência de preservação encontrou 3.399 arquivos com hashes inalterados.
Os testes usaram dados temporários e apenas copiaram o perfil original.
Não foram alterados renderer, configurações gráficas, originais ou saves;
não houve push, Release, restauração de Iruka ou regressão de outros jogos.

Os recibos antigos de outros jogos também referenciam o lock anterior. Eles
não foram repreparados ou executados neste escopo; precisam da preparação normal
antes do próximo lançamento. Não se atribui compatibilidade nova ao restante
do acervo por essa correção.

## Reprodução focada

Os comandos abaixo são referências; o teste local abre somente o Motocross,
sem enviar comandos de teclado/controle, e encerra pelo limite do supervisor.

```powershell
scripts/devshell.cmd cmake --build .local/build/win-amd64 --config Debug --target x360n_sdk_checks x360n_ctr_dispatch_native x360n_bounded_byte_native -- -j4
.local/build/win-amd64/tests/Debug/x360n_sdk_checks.exe '[ctr-dispatch],[aot-discovery],[gapfill]'
.local/build/win-amd64/tests/Debug/x360n_ctr_dispatch_native.exe
.local/build/win-amd64/tests/Debug/x360n_bounded_byte_native.exe
powershell -NoProfile -File scripts/build-title.ps1 -Title motocross_madness
powershell -NoProfile -File scripts/build-title.ps1 -Title dash_17559
powershell -NoProfile -File tests/local/motocross-shared-xam.ps1 -Supervisor .local/build/desktop-only/src/supervisor/x360n.exe -Seconds 55 -GuideAfter 30
```

O teste local agora exige, além dos marcos de avatares/Guide, duração completa
até o timeout previsto, sem cancelamento, linha crítica ou chamada não registrada.
Uma queda após registrar `READY` deixa de poder passar apenas por encontrar
essa mensagem no log.
