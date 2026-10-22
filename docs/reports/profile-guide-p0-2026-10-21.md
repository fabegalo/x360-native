# P0 — perfis e conquistas no Guide original 17559

Base: `main`, `3effaac5d4fb921493e3ac9044ca74ffd15840f0`.
**O P0 não está concluído. A queda original ainda não tem causa confirmada.**
Foram implementados contratos de navegação, edição, remoção recuperável e
preferências locais, além do diagnóstico da queda. Os testes de contratos não
substituem o aceite das cenas originais. Quatro usuários simultâneos continuam
sem implementação/certificação; a sessão local permanece no slot 0.

## Evidência existente

A sessão do mantenedor `20261021T095029Z-session-0-dash_17559` já reproduz o
problema: criação às 06:51:05.838, Logon `0x24` com sucesso e leitura inválida
de `0x00000005` às 06:51:05.913, thread guest `0xF80002CC`. O relatório contém
`crash: null`; não contém pilha, endereço da instrução host nem registradores
PPC. A reprodução foi reaproveitada, conforme orientação do mantenedor.

Os avisos de `XamProfileOpen`, `DASHUSER:` e os outros stubs próximos não
identificam a instrução culpada. A captura de 18/10 em
`20261018T105434Z-session-2-dash_17559/crash.txt` corresponde a outro acesso,
`0x00000020`; sua pilha não pode ser atribuída à ocorrência atual. Os contratos
existentes já possuem testes de criação, migração, troca, falhas e overlapped;
isso não demonstra que o retorno da interface original esteja correto.

Não houve nova execução de dashboard, Computer Use ou jogo pelo agente. Não foi aplicada
correção especulativa, retorno fictício de sucesso ou tolerância a ponteiro inválido.

### Sessão manual posterior: 20:02–20:03

A pedido do mantenedor, foram lidos os registros de
`.local/evidence/runs/20261021T230236Z-session-0-dash_17559`. O processo durou
36.315 ms e terminou com código `0`, sem timeout e com `crash: null`.
Os 14 originais foram verificados pelo runtime, usando Xenos/D3D12.

- Às 20:02:59.693, a navegação chamou `XamShowGamesUI`, ainda `STUB`.
- Às 20:03:07.858, chamou `XamShowAvatarAwardGamesUI`, também `STUB`.
  Ambas estavam declaradas como `REX_EXPORT_STUB` em
  `external/rexglue-sdk/src/kernel/xam/xam_ui.cpp`; as chamadas não comprovam
  que as respectivas telas tenham aberto.
- Às 20:03:12.642, `XamLoaderRebootToDash` recebeu `reason=23`, copiou os
  1020 bytes de contexto e solicitou retorno à dashboard. O handoff foi
  `kind=4` (`Dashboard`). O supervisor encerra a sessão quando esse retorno
  parte da própria dashboard (`src/supervisor/main.cpp`), explicando a saída
  normal observada; não foi uma exceção de memória.

Não há chamada de CreateProfile nem captura `rex.log.ppc.txt`/`crash.txt`
nessa sessão. Ela evidencia entradas de navegação ainda não implementadas,
mas não reproduz nem resolve a queda após criação/login. A sessão usou o
armazenamento habitual `.local/user/dash17559`; não foi a execução isolada do
roteiro de diagnóstico. Nenhuma interação de UI ou novo teste foi realizado
pelo agente nesta inspeção.

## Implementação após a inspeção

O SDK permanece no commit pinado. Os 114 patches anteriores foram preservados;
acrescentados 0116–0119, com hashes e preimagens no lock. Os campos `files` das
entradas históricas acompanham o hash final, conforme o mecanismo já existente;
os bytes dos patches anteriores não foram alterados.

| Patch | Mudança verificada no contrato |
|---|---|
| 0116 | `XamShowGamesUI` e `XamShowAvatarAwardGamesUI` passam ao GamerProfile original, modos 1/6, FE/21008 e 544 bytes. Preservam o XUID inteiro de r4. |
| 0117 | `XamShowEditProfileUI` abre o modo 3. `XamProfileRenameAccount` recebe XUID em r3 e WCHAR em r4; grava o nome antes de publicá-lo, mantém XUID/sessão/dados e publica a notificação 14. |
| 0118 | `XamProfileDelete` arquiva e sincroniza o snapshot anterior em `profiles/deleted/` antes de remover a identidade. Preserva saves, configurações e conquistas. V2 vazio impede que o v1 de migração ressuscite o último perfil removido. |
| 0119 | Preferências INT32, INT64, DOUBLE, FLOAT, DATETIME, WCHAR e BINARY deixam de retornar sucesso sem gravar. Gravação e cache seguem XUID/Title ID, com validação dos buffers e resultados de falha/overlapped. |

Os modos e registradores foram conferidos nos exports do original 17559:
2D0 → `816C48A0`, 3C7 → `816C49F0`, 3BC → `816C4B40`,
236 → `816DE888` e 233 → `816DB078`. O caller de renomeação do
GamerProfile (`9010BC20`) confirma o XUID completo em r3.
Não foram substituídas cenas, textos, imagens, dados de conquistas ou autenticação.

Preferências gerais usam `profiles/<XUID>/settings-v1/<ID>.dat`; escalares
e WCHAR são big endian, com terminador zero para strings. Configurações
específicas de título conservam o caminho e os bytes anteriores. O commit
atômico é por valor; falha de I/O no meio de um lote não promete rollback dos
valores já confirmados. Tipos/tamanhos/buffers de todo o lote são validados
antes do primeiro commit. Arquivo corrompido gera erro, sem fabricar seu valor.

Leitura e escrita das configurações usam a mesma seção crítica do logon
até terminar a cópia da resposta. Isso impede substituir/liberar um objeto
`Setting` que outra chamada ainda lê e impede limpar o valor durante essa
cópia numa troca de usuário. Abertura de arquivo malsucedida também deixa de
chegar a `fwrite`/`fclose` com FILE nulo. São correções demonstradas nos
contratos; nenhuma delas foi atribuída à queda guest `0x00000005` sem pilha.

### Regressões e execução original sem janela

Os casos novos falharam antes dos respectivos patches (`navigation-red.log`,
`edit-red.log`, `delete-red.log`, `settings-red.log`). Depois das correções:

- Navegação inicial: 558 asserções / cinco casos.
- Edição/renomeação: 760 asserções / seis casos.
- Remoção, migração e navegação: 1.194 asserções / 20 casos.
- Preferências: 102 asserções / quatro casos, incluindo sete tipos, reinício,
  troca de usuário, falha de escrita, corrupção, buffers, overlapped e concorrência.
- Rodada final relacionada ao incremento: **2.060 asserções / 31 casos**,
  tags `local-profile`, `achievement-ui`, `profile-settings`,
  `offline-achievement-api`, `save-storage` e `guest-fault-context`;
  `p0-focused-final.log`. Não foi executada a suíte completa.

A sonda `x360n_profile_17559_probe`, sem janela, renderer ou entrada, executa
os métodos originais `9010B0B0` (CreateProfile) e `90103528` (Logon), usando
armazenamento temporário novo. Dois perfis distintos foram criados e conectados
por esses métodos: criação retorna zero, Logon retorna o pending original
`8000000A`, com resultado overlapped zero e XUID selecionado correto.
Isso valida o trecho da ABI real, sem executar o retorno de cenas à dashboard.
Os originais são conferidos por SHA antes de carregar; nenhum entrypoint de
dashboard/jogo ou UI foi executado. Logs em `original-contract-probe.log` e
nas execuções `*-profile-p0-original-contract`.

A primeira tentativa da sonda usou caminhos relativos ao diretório do filho;
foi corrigida para caminhos absolutos. No sandbox, a criação retornou
`8007065B`, sem isolar a causa interna; a execução acompanhada fora dele passou.
O teste overlapped
de preferências inicialmente estava sem thread guest, contrariando o contrato
de completion do SDK; a fixture foi vinculada a `XHostThread` e libera essa
thread antes de recriar o runtime. Não se atribuem essas falhas de fixture ao
crash relatado pelo mantenedor.

## Diagnóstico acrescentado

A 17559 instala um observador de falhas somente quando inicia com debugger e
`log_file` definido. A opção já existente do launcher **Diagnóstico → Capturar
pilha de falhas nas próximas sessões** usa `--crash-stack` no supervisor e
satisfaz essas condições. Nenhum observador ou arquivo é criado numa sessão
normal sem debugger. A instalação acontece antes da execução guest.

O observador usa a API existente de exceções do SDK, depois do handler de MMIO.
Para acessos guest recusados pelos handlers anteriores, registra em
`rex.log.ppc.txt`, ao lado de `rex.log`:

- PC host, thread host/guest, operação e endereço acessado;
- GPRs r0–r31 em 64 bits, LR, CTR, último destino indireto, CR e FPSCR.

O snapshot lê o `PPCContext` da thread corrente. Código AOT pode manter valores
mais recentes em variáveis host; o arquivo explicita essa limitação. LR e último
destino indireto **não são o PC guest**. A pilha nativa, capturada pelo supervisor
em `crash.txt`/`report.json`, continua necessária. Nomes e linhas dependem dos
símbolos disponíveis no build; endereços sem símbolos não identificam sozinhos
a função original. A instrumentação não atribui uma função por aproximação.

O arquivo é aberto antes da execução e nunca sobrescreve evidência existente.
O callback usa buffer fixo, não lê memória guest nem chama o dispatcher e sempre
retorna `false`: não altera registradores, PC de retorno ou política de exceções.
Há proteção contra reentrada e limite de 16 registros. Um handler posterior
ainda pode tratar uma exceção observada; somente a segunda chance do supervisor
certifica uma falha não tratada. A instrumentação não corrige o crash.

## Validação

Evidências locais em `.local/evidence/validation/profile-p0-20261021/`.

- Build Release final de `x360n_sdk_runtime_checks` e da fixture sintética
  concluído: `build-tests-final.log`.
- `[guest-fault-context]`: **59 asserções em dois casos aprovadas**
  (`tests-fault-context.log`). Seis subprocessos sem janela verificam captura
  de leitura real em guest `0x00000005`, modo desligado, remoção do observador,
  thread host sem contexto PPC, exceção tratada antes do observador e exceções
  tratadas depois dele. O último caso preserva registradores e limita 20
  exceções a 16 registros. Os testes também cobrem arquivo existente, destino
  indisponível, instalação repetida e reinicialização após `Stop`.
- Quatorze originais Retail 17559 conferidos pelos hashes registrados em
  `original_assets.h`: `original-assets.json`. Nenhum recurso de outra versão
  foi usado.
- SDK e os 114 patches existentes permanecem inalterados; o build verifica
  seus hashes pelo mecanismo existente. Nenhuma suíte completa foi executada.

## Build anterior do diagnóstico (antes dos patches 0116–0119)

O driver oficial `scripts/build-title.ps1 -Title dash_17559` concluiu em Release,
saída 0, com SDK pinado e conferência dos patches. A tentativa final foi
acompanhada fora do sandbox, com quatro tarefas de compilação; run
`ddf04266ca244fffafadeea6a09ec451`, log `build-dash-release-final.log`.
As duas primeiras tentativas foram interrompidas após períodos sem nova etapa;
outra tentativa foi encerrada durante a atualização das ferramentas. Essas
tentativas não certificam build. O driver descartou seus intermediários sem
autenticação e realizou a reconstrução segura antes de publicar o recibo final.
Nenhum processo de dashboard/jogo do mantenedor foi encerrado.

Os **12 artefatos** do recibo final passaram na conferência independente de
SHA-256 (`final-products.json`); o recibo inclui os novos fontes do diagnóstico.
O marcador de preparação incompleta está ausente. Não houve edição manual do
recibo ou cópia de DLL para substituir um artefato de produção.

Uma cópia isolada da fixture sintética foi executada com a DLL `rexruntime.dll`
exata desse produto, conferida por hash. O supervisor capturou a exceção
`0xC0000005` esperada; o PC host e a thread do snapshot PPC coincidiram com os
da captura nativa, e r3 conservou o sentinela de 64 bits. Resultado em
`product-probe-validation.json`, com o relatório e `fault.ppc.txt` preservados.
Essa saída de falha é intencional na fixture; não é execução da dashboard nem
reprodução certificada da queda de CreateProfile. Nenhum original foi carregado
por essa sonda, e nenhum perfil ou conquista foi criado.

Os **dez arquivos existentes** de identidade e conquistas verificados mantiveram
seus hashes após toda a preparação e a sonda (`preserved-before.json` e
`preserved-after.json`). Isso inclui os registros persistentes usados pelo
mantenedor; a conquista 155 não foi alterada. Não houve escrita em saves.
O roteiro manual passou somente pela análise sintática de PowerShell; não foi
executado pelo agente (`manual-script-syntax.log`).

## Preparação integrada e aceite pendente

### Build integrado e conferência final

O driver oficial `scripts/build-title.ps1 -Title dash_17559` concluiu com
saída zero, run `ec2f13ce3a8743cf87189cef74770243`. A ferramenta e a geração
dos cinco manifests concluíram; a compilação final da 17559 levou 362 s.
Logs em `build-p0-final-implementation.log` e nos arquivos desse run em
`.local/build/titles/dash_17559/`. O recibo final tem **12 artefatos**,
conferidos por SHA-256; inclui o lock com os quatro patches novos e não há
marcador de preparação incompleta (`implementation-products.json`).

Duas sondas sem janela foram executadas com a DLL `rexruntime.dll` exata
desse produto, em cópia isolada: os dois pares de criação/logon originais
passaram; a falha sintética manteve `C0000005`, com PC e thread host do
snapshot PPC idênticos aos do relatório nativo. Referências e hashes em
`implementation-probes-final.json`. A falha sintética esperada não é uma
execução nem uma reprodução da queda na UI.

O supervisor também foi corrigido para o evento observado às 20:03:12:
`Dashboard` solicitado pela própria dashboard agora preserva o contexto e
reinicia o mesmo perfil verificado, em vez de encerrar a plataforma. Flags
ausentes do manifest continuam recusadas; fechamento de janela, saída normal,
desligamento, crash e watchdog mantêm seus caminhos. A regressão sintética
falhou antes da correção (`return-red.log`); a rodada final relacionada passou
em **57 asserções / quatro casos** (`return-green.log`). A primeira comparação
do log no teste foi ajustada para aceitar os finais de linha do Windows;
`return-first-green-fixture.log` preserva esse resultado intermediário.
O supervisor usado pelo aplicativo, em `.local/build/desktop-only`, foi
recompilado em Release (`build-desktop-supervisor-final.log`).

Somadas as duas rodadas finais: **2.117 asserções / 35 casos**, sem suíte
completa. Os **14 originais 17559** continuam com seus hashes esperados.
Os bytes dos **114 patches anteriores** não mudaram, e o lock contém agora
118 patches ativos. Os **dez arquivos reais de identidade/conquistas**
verificados antes e depois da implementação ficaram idênticos; a conquista
155 não foi apagada ou reenviada. Conferências em
`patch-preservation-final.json`, `original-assets-implementation.json`,
`preserved-{before,after}-implementation.json` e
`implementation-final-validation.json`. Não houve gravação em saves reais.

### Erros das sessões manuais das 21:02–21:08

Inspeção dos quatro runs `20261022T000257Z`, `000356Z`, `000450Z` e
`000651Z`, todos `session-0-dash_17559` (UTC 22/10; horário local 21/10).
O run `000450Z` caiu às **21:06:14.169**, saída **0xC0000409**, ao resolver
`gp#803_SignInPreferences.xur`. O log registra `CTR=817ACC88`,
`LR=817B3928` e chamada não registrada em **0x817ACC88**. É falha fatal
de despacho, não evidência de estouro de buffer: esse código de saída também
cobre abort/fastfail. `report.json.crash` é nulo; não houve captura de pilha
nessa sessão. Não confundir com o acesso guest 5 após CreateProfile.

O original 17559 confirma duas instruções em `817ACC88..817ACC8F`:
`mr r3,r5; b 817CB4B0`. O endereço é formado em `817B92F0`, está fora
da `.pdata`, e o destino já era gerado. Adicionada somente a entrada original
à análise `xam_ui_17559.toml`; nenhum handler substituído por sucesso.
A sonda headless ganhou `--signin-preferences-only`: com a DLL anterior,
recebeu `DEADBABE` (trap sem função), saiu 10 e falhou como esperado no run
`20261022T001215Z-p0-preferences-red`. Usa mensagem ignorada pelo handler,
sem construir cena, gravar preferências ou executar entrada/UI.
Com a DLL recompilada, o mesmo comando passou no run
`20261022T001520Z-p0-preferences-green`: resultado 0 e marcador do campo
`handled` preservado (`13572468`), sem crash. O SHA-256 da nova
`dash_17559_xam_ui.dll` é
`c5cbda4bc533202ea8d23cf3901d9e39a1f458cea4c07d3b43dd81d333b805ab`.
Isso comprova o despacho do callback original; não é aceite da cena inteira.

Os outros três runs terminaram com saída 0 após `XamLoaderRebootToDash`
(reasons 23/0/0). A cópia adjacente ao launcher em
`.local/build/desktop-only/src/desktop/x360n.exe` ainda era antiga
(`f3d10819…98836`), diferente do supervisor recompilado
(`37ac1501…b3212`). Isso corrige a afirmação anterior de que recompilar
o supervisor já havia atualizado a cópia usada pelo aplicativo.
O staging dos executáveis passa a ocorrer em todo build do launcher,
inclusive quando só o supervisor muda e o launcher não precisa relinkar.
O build completo do launcher atualizou os auxiliares, mas o link de
`x360-native.exe` falhou com `permission denied` (arquivo indisponível para
substituição). Nenhum processo do usuário foi encerrado. O alvo isolado
`x360n_stage_desktop_tools` passou em seguida e confirmou hashes iguais
nas duas cópias do supervisor: `b90cd4e8…e2e9c`. A correção funcional está
no supervisor e esse staging não depende de substituir o frontend aberto.

Outras pendências observadas, sem atribuí-las à queda: setting IDs
`63E80044`, `7008004F`, `61180050`, `10040057`, `40520041`, `41040040`,
`43E80043`; dispositivo `DASHUSER:` ausente; abertura de
`media:\pt-BR\shrdres.xzp` falhando repetidamente; `XamShowOptionsUI`
ainda stub; mensagem XGI `000B0082` não implementada às 21:07:42.839;
XLIVEBASE `00058003` repetido milhares de vezes. Estes logs não demonstram
lista/detalhes de conquistas funcionais nem justificam silenciar esses erros.

Evidências locais adicionais: `manual-errors/` dentro da pasta de validação
P0, com resumo dos quatro runs, disassembly/xref original, hashes das cópias
do supervisor, logs de build e resultados das sondas focadas.
O driver oficial concluiu o build Release no run
`cdc6e59b93d64c7b85927217e07537f9`; recibo publicado às 21:16:02,
12 artefatos conferidos e marcador incompleto ausente. Os 14 originais e
dez arquivos de identidade/conquistas não mudaram durante esta correção
(`manual-errors/final-validation.json`). Não foi repetida a suíte completa
nem executada a interface pelo agente.

### Correções dos contratos observados nas sessões manuais

Validação adicional em `.local/evidence/validation/profile-p0-followup-20261021/`.
Os patches 0123, 0124 e 0125 corrigem três falhas independentes dos logs:

- Configurações opcionais válidas e ainda não gravadas retornam `NO_VALUE`/`UNSET`,
  sem invalidar o lote inteiro com erro 87. Chaves malformadas e arquivos
  corrompidos continuam sendo erros. O teste usa os sete IDs registrados acima.
- `XamShowOptionsUI` envia o pedido original FE/21015 de 12 bytes ao HUD,
  conforme `816C2BD8 -> 816C2AF0`; não altera o usuário ativo.
- XGI B0082 lê os flags persistidos `10040053` do XUID ativo, seguindo
  `8190B3B0 -> 8190A7D0`, incluindo as preferências compostas e S_FALSE.
  Saída e OVERLAPPED são validados antes do despacho; não há serviço Live.

Os três casos falharam antes dos patches (`contracts-red.log`). As verificações
intermediárias passaram: 446 asserções/oito casos para configurações e APIs de
conquistas, 619/dez para opções/navegação/UI de perfil e 221/sete para preferências.
Não somar essas contagens como casos distintos: os filtros se sobrepõem.

O patch 0128 remove os resultados fictícios C0DE do parser `XamParseGamerTileKey`.
O export original 30A em `81670A50..81670B68` lê um descritor XAM, três campos
hexadecimais de oito WCHAR e o fallback reservado. O teste vermelho confirmou
a divergência do primeiro componente; após o patch passaram 21 asserções/um caso.
Isso corrige a chave de uma imagem; seleção, escrita e cache de gamerpic/avatar
ainda não foram demonstrados.

A auditoria da família do callback fatal identificou outras 23 entradas
originais de duas instruções, entre `817ACC38` e `817ACCF0`, fora da `.pdata`.
Todas fazem `mr r3,r5; b <handler já gerado>` e seus endereços são formados
pelo código original. Foram incluídas como funções na análise, sem substituir
os handlers. Imagem auditada SHA-256
`235c5f6aa71a98ec7ec94d7e825198bf97c38099af9bb174ad2ab0642b182d07`;
evidências `profile-callback-xrefs.log` e `profile-callback-audit.json`.
Após gerar o código, os 24 callbacks e todos os seus destinos constam dos
mappings. A conferência das 13.799 funções com `.pdata` encontrou zero
extrapolações de limite (`profile-callback-{mappings.json,boundaries.log}`).

Após os novos contratos e as alterações compartilhadas de Storage/gráficos,
o build Release isolado passou. A união focada de perfis, conquistas e Storage
passou **2.690 asserções em 43 casos**, exit 0 (`profiles-storage-final.log`).
Inclui os filtros de dispositivo, seletor, saves, enumeração agregada e
notificações solicitados pela sessão de armazenamento. Não é suíte completa
nem aceite visual. As sessões de Storage e gráficos foram coordenadas com
autorização explícita do mantenedor para congelar os arquivos compartilhados
durante um único preparo oficial da 17559 em Xenos/D3D12.

As sondas novas de operações originais do GamerProfile e de comparação do
parser não executam entrypoints, cenas, GPU, entrada ou jogos; usam armazenamento
temporário novo. A primeira versão da sonda de conquistas leu uma resposta
ainda não preenchida e caiu: erro da sonda, corrigido com guarda de conclusão.
Depois foi identificado que o worker de conclusão exige um módulo de processo;
o fixture agora o configura sem executar sua entrada. A tentativa seguinte
encontrou as DLLs temporariamente removidas pelo preparo oficial concorrente
e saiu antes da carga. Esses resultados não comprovam falha nem sucesso das
cenas da dashboard. A verificação final abaixo substitui essa pendência técnica.
Uma guarda adicional de strings da sonda inicialmente usou uma API inexistente
da classe Memory; a compilação recusou o fixture. Foi substituída pelo validador
de spans já existente no SDK e o build isolado passou
(`original-probe-final-build2.log`); nenhum runtime foi modificado para isso.

### Produto integrado e operações originais finais

O primeiro preparo compartilhado (`b6b4fb1b10324bca82677b8a23ccb16d`)
falhou estritamente no novo `deviceselector.xex`: salto em `916D7BFC` para
`916D7BAC`, fora das funções descobertas. A sessão de armazenamento conferiu
o corpo original e delimitou o byte remainder de memcpy em 36 bytes, sem
relaxar a validação ou mudar o SDK. O codegen focado passou. Essa sessão também
conferiu os adapters dos imports do seletor antes de retomar o preparo.

O driver oficial concluiu o run **93e20209d30b41088060e65861f0524e**, exit 0,
com recibo publicado às **22:19:03 de 21/10**, Xenos/D3D12. Logs em
`.local/evidence/storage-p0/title-product-build.*`. O utilitário experimental
de preflight Vulkan ficou condicionado a builds Vulkan; não participa desse
produto D3D12. Conferência independente em `final-validation.json`:

- 13 artefatos e 4.027 entradas do recibo, zero divergências; marcador incompleto ausente.
- 15 módulos/recursos originais da 17559, todos com os SHA-256 registrados.
- SDK ainda em `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`; lock com 128 patches.
- Bytes dos 114 patches históricos preservados; dez arquivos reais de identidade,
  metadados e ledger mantiveram seus hashes, inclusive o histórico da conquista 155.

Recibo SHA-256: `e5a628194ca9665b150161c2d0d98508816edc32a9d09d13ebdd92b1db0cd69d`.
Lock SHA-256: `aba4c1e8f459f51f595e6c586d802d4ac2046071e65d04f7d1a0fa42adbcf7dd`.
A aplicação limpa dos 128 patches reproduziu os 130 arquivos finais do SDK,
também conferida independentemente pela sessão gráfica. Existem três divergências
legadas nos registros individuais `base_files`, documentadas pelo replay de
Storage; não há divergência de aplicação ou dos hashes finais nem dos patches novos.
Os registros históricos não foram reescritos para ocultá-las.

Sonda com cópia privada da **rexruntime.dll exata do produto**, módulos nativos
finais no diretório oficial e originais verificados antes da carga:

- `20261022T011923Z-p0-original-achievements-final`: passou em 1.465 ms.
  Executou os métodos originais GamerProfile `90120CD0` e `901200D8`, incluindo
  os pedidos originais de enumeração overlapped, para dois títulos de fixture e
  Alpha → Bravo → Alpha. Em cada passagem, retornaram dois jogos/duas conquistas;
  Alpha recebeu seus 10 pontos e flag de desbloqueio, Bravo ficou com zero.
  Nomes/descrições vêm do XDBF de teste. Nenhum dado fictício foi instalado no produto.
- `20261022T011944Z-p0-original-tile-final`: passou em 573 ms. Comparou os três
  componentes e o comprimento retornados pelo export original `81670A50` com
  o HLE em seis entradas (chaves normais, reservadas, zero e buffer nulo).

Ambos terminaram com exit 0, sem crash, stubs, mensagens não implementadas,
erros ou críticas. Os hashes usados nas sondas continuaram idênticos ao produto.
Usaram armazenamento temporário novo; não executaram entrada de módulo, cenas,
renderer, controle ou jogos. Isso demonstra as operações de dados originais,
sem certificar a navegação inteira nem a estabilidade após criação.

O roteiro manual continua sem ser executado pelo agente. Sua guarda agora exige
Xenos/D3D12 e foi validada pelo parser PowerShell. O aceite manual foi solicitado
após concluir e conferir o produto; permanece pendente até resposta do mantenedor.

### Erro manual após criar perfil: fechamento da pilha

Run `20261022T012207Z-session-0-dash_17559`, 22:22:07–22:22:38 de 21/10,
produto `93e…`: criação e logon retornaram 0; a queda `C0000005` veio depois.
O mantenedor esclareceu que Entrar → Criar perfil grava a identidade e fecha
depois, enquanto o atalho direto do Guide não chega a criar. Nenhuma rota está
aprovada. `crash:null`, sem arquivo PPC: não há caller/endereço confirmado.

Sequência do log: Guide → Sign-in → CreateProfile → outro pedido CreateProfile
→ fechar → restaurar CreateProfile concluído (`E_ABORT` na cena) → restaurar
Sign-in → queda antes de concluir a inicialização. Encontrado defeito concreto:
o SDK tratava `XamAppUnloadStack(0)` como `XamAppUnloadSelf`, embora o host
mantenha pais suspensos. Export original 246: `816CAF08 -> 816CAD78`.
Update original CreateProfile `90106398`: estado `+84=2` pede Stack; 1 pede Self.

Patch 0129 preserva `close_stack_requested` até o limite do frame. O host
descarta os pais quando o original pede Stack, não serializa o app concluído
e mantém o sucessor enfileirado. Um close-self posterior não reduz o escopo.
Destruição/descarga continuam após o callback. É correção de contrato comprovada;
o vínculo com o acesso inválido exato ainda exige aceite/captura manual.

Para descartar Sign-in, o host libera as quatro strings de 34 bytes duplicadas
por `90106EC0`: estado de 956 bytes, ponteiros `252 + 232*i`. A restauração
`90106CF8` normalmente consome essas cópias. O helper é exclusivo da 17559.
A sonda executou o salvamento original, conferiu cópias distintas, liberação
das quatro alocações e preservação das strings de origem, sem cenas ou GPU.

Evidências na pasta `profile-p0-followup-20261021`:

- `stack-red.log`: 23 asserções, duas falhas antes do patch.
- `stack-only-green.log`: as mesmas 23 passaram.
- `stack130-green.log`: 1.154 asserções/22 casos passaram, incluindo o cache
  offline 0130 da sessão de Storage, sem suíte completa.
- O filtro ampliado revelou uma expectativa incorreta do teste ANY_USER:
  aguardava um key-up de uma sessão encerrada. Agora exige ausência do evento
  antigo e um novo toque no controlador correto, preservando exclusividade.
  Não foi alterado código de entrada do SDK. Falha/reprodução isolada mantidas
  em `stack-green.log` e `stack-anyuser-isolated.log`.
- `20261022T013434Z-p0-original-close129-green`: método original distingue
  Self/Stack; exit 0 em 618 ms, sem erros/stubs.
- `20261022T014001Z-p0-original-signin-save130`: serialização/limpeza original;
  exit 0 em 725 ms, sem erros/stubs. Essas sondas usam o runtime dos checks,
  não certificam o produto final ainda em preparação.
- `close130-replay.json`: 0129/0130 reaplicados ao preimage 128 reproduziram
  os dois arquivos finais por SHA-256. Check integral da pilha 130 passou.

Uma corrida entre as sessões duplicou o hook opcional do seletor no começo do
preparo. O processo próprio foi interrompido cedo, logs preservados, duplicação
removida e um include/um hook conferidos antes de reiniciar com fontes congeladas.
Uma tentativa seguinte falhou ao compilar o novo diagnóstico por falta do
include que define `XAM_OVERLAPPED`. Storage corrigiu a dependência do header;
o preparo `c513…` então compilou e concluiu. Não foram executados testes sobre
um produto incompleto dessas tentativas.
O diagnóstico é desligado por padrão; sua execução visual pertence à autorização
própria de Storage, sem transferir autorização de Computer Use para perfis.

A proteção dos dados usa agora uma referência de **15 arquivos após o teste
manual** (`after-manual-user-data-before.json`), incluindo a identidade criada
pelo mantenedor. A comparação anterior de dez arquivos é histórica; alterações
manuais legítimas não serão atribuídas às sondas isoladas.

### Pedido durante inicialização e estado recusado pelo original

O host só enfileirava o sucessor depois de instalar `draw_ui`. Entre
`BeginSystemUi` e a primeira cena, rejeitava esse mesmo pedido porque o app pai
já estava registrado. O teste `[profile-ui-initial-request]` usa o host real
e `XamShowCreateProfileUI`, representando essa fase sem inicializar módulos,
executar frames, cenas ou renderer. Antes: duas falhas em sete asserções
(`initial-request-red.log`). Depois: pedido aceito, fechamento adiado e posse
do pai mantida até a fronteira correta.

Outra diferença de contrato foi comprovada no método original Sign-in
`90106EC0`: com a flag `+96 & 0x100000`, retorna `S_FALSE=1` sem escrever o
buffer. O host agora só suspende/restaura estado quando o retorno é `S_OK=0`;
um HRESULT positivo não comprova um buffer válido. A sonda
`20261022T015646Z-p0-original-declined-save130-linked` passou em 636 ms,
incluindo as quatro cópias liberadas no caso normal e o buffer intacto quando
o salvamento é recusado. Tentativas anteriores terminaram antes dos métodos
originais porque o DLL de Sign-in ainda não havia sido ligado pelo preparo.

Build dos checks concluído com exit 0. O filtro relacionado a perfis, entrada,
conquistas, seletor e cache offline passou **1.161 asserções em 23 casos**
(`initial-request-green.log`), sem suíte completa. Esses defeitos estão
demonstrados em CPU; sua relação com cada rota visual e com o endereço exato
da queda continua sem confirmação manual.

O preparo `c513efb94fec49ff87e9c53ec836f77f` terminou antes desses três últimos
ajustes do host: 13 artefatos, 4.031 entradas e 15 originais conferidos pela
sessão de Storage. Ele é a base para o preparo incremental final, com SDK,
lock e fontes de produção congelados entre as sessões. Não é o produto final
dos três ajustes acima.

### Produto final e contratos originais após os ajustes

Preparo oficial `69e5326336be4885a0aa0f06aded5123` concluído às 23:05 de
21/10, com fontes congeladas entre as sessões. Build incremental invalidou
dois objetos e dois produtos; compilação/link passaram em 15 s. Recibo
`8037f518e470a5c0c575cd81305af4fd7687175eb7d7b7dd2019dfe7faeaa774`:
**13 artefatos, 4.031 entradas e 15 originais**, todos conferidos por SHA-256;
marcador incompleto ausente. Os 114 patches históricos e os 15 arquivos reais
da referência pós-teste manual permaneceram idênticos. SDK pinado e lock com
130 patches preservados (`close-stack-final-validation.json`).

Quatro sondas CPU usaram cópia privada da DLL exata do produto,
`ced26f971765babef7c0f7bb0c1cbe359337f3fde420e844dfea3fa6620477ba`:

| Run de 22/10 UTC | Contrato original | Duração |
| --- | --- | --- |
| `20261022T020607Z-p0-close-final130` | Self/Stack por `90106398` | 686 ms |
| `20261022T020608Z-p0-signin-state-final130` | quatro cópias e recusa S_FALSE de `90106EC0` | 682 ms |
| `20261022T020609Z-p0-create-logon-final130` | duas criações e dois logons | 588 ms |
| `20261022T020610Z-p0-achievements-final130` | jogos/conquistas por dois XUIDs, Alpha → Bravo → Alpha | 1.394 ms |

Todas saíram 0, sem erros, críticas, stubs, chamadas não registradas ou mensagens
não implementadas. Runtime e recibo mantiveram seus hashes durante as sondas.
`close-stack-final-contracts.json` registra os resultados; armazenamento novo
e temporário em cada processo, sem entrypoints de módulos, cenas, GPU ou jogos.
Essa prova não certifica a criação nem a navegação de conquistas na interface.

Integração posterior de Storage: o teste visual revelou callback numérico
original `916D7560` ausente no AOT do seletor. O preparo
`5898c6e442c94a199dc9dedbabeffdd3` acrescentou limites dos helpers no manifesto
próprio. Comparação dos recibos demonstrou que **somente
`dash_17559_deviceselector.dll` mudou**; executável, host de UI, runtime e todos
os módulos usados pelas quatro sondas de perfis mantiveram seus hashes.
Não foi necessário repetir essas sondas. Recibo integrado
`8002151ae9b64274fe409c3fcd694568a54267c8e16659080435a73c98d21a79`:
13 artefatos/4.031 entradas/15 originais conferidos integralmente, 15 arquivos
reais e 114 patches históricos preservados, sem marcador incompleto
(`selector-integrated-final-validation.json` e
`selector-integrated-artifact-comparison.json`). O aceite visual do seletor
é registrado separadamente pela sessão de Storage; não certifica perfis.

### Resposta manual e interferência da sessão de diagnóstico

O mantenedor respondeu “Cria e fecha novamente” após o pedido de teste. A
correlação mostrou que a interação ocorreu no run de Storage
`20261022T021536Z-storage-selector-cancel`, em armazenamento descartável,
e não em um novo run aberto pelo aplicativo principal. Sequência registrada:
Entrar → CreateProfile → seletor → criação/logon de Player1 às 23:16:36,
retorno ao Guide às 23:16:38 e mais navegação até 23:16:47. A recusa
`S_FALSE` de Sign-in foi tratada corretamente, sem suspender aquele buffer.

O processo terminou com `FFFFFFFF` após 71.174 ms. A sessão de Storage
confirmou que encerrou intencionalmente seu próprio PID 55676, identificado
pelo armazenamento/label do diagnóstico, para liberar o teste físico.
Sem exceção capturada nesse run; isso não comprova uma nova queda de perfis,
nem serve como aceite funcional. O aviso de teste foi emitido antes de obter
a confirmação de que todas as janelas de diagnóstico tinham encerrado;
a interferência foi comunicada ao mantenedor. Nova verificação manual
solicitada pelo aplicativo principal com as outras sessões paradas e sem
produtores/UI concorrentes. Dados temporários dessa interação preservados.

### Aceite pendente nas cenas originais

No próximo teste manual autorizado, ativar a captura de pilha em Diagnóstico
antes de iniciar a Retail 17559 e concluir a criação pela interface original.
Usar armazenamento de teste isolado; não é necessário iniciar Forza nem
desbloquear qualquer conquista. Preservar juntos `rex.log`, `rex.log.ppc.txt`,
`crash.txt` e `report.json` da nova sessão. Arquivo PPC vazio indica ausência
de acesso guest observado, não sucesso do fluxo.

Para abrir manualmente uma sessão com armazenamento novo e limite de 90 s por processo:
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/validation/start-profile-diagnostic-17559.ps1`.
O script não foi executado pelo agente. Não injeta entrada; a navegação cabe ao
mantenedor com o controle físico. O supervisor confere a preparação e preserva
o armazenamento de teste ao encerrar. A opção de Diagnóstico do launcher, sozinha,
não isola o armazenamento; usar o script quando for necessário esse isolamento.

A comprovação da estabilidade após criação e o aceite das cenas permanecem
pendentes. As APIs de edição/remoção/navegação implementadas acima não
certificam todas as telas do perfil nem o fluxo completo de conquistas.
O roteiro pode reutilizar uma pasta isolada criada por ele com
`-TestStorageRoot <pasta>`, permitindo conferir persistência após reiniciar;
recusa pastas sem seu marcador e fora de `.local/diagnostics`.
Continua sem entrada sintética ou lançamento automático de jogos.

Na pasta isolada: criar dois perfis, alternar, editar nome/preferências,
reiniciar e verificar persistência, cancelar a exclusão e depois confirmar
a remoção de uma identidade de teste. Para consultar a conquista 155 já
existente, usar a conta habitual pelo aplicativo, com captura de pilha ativa:
Guide → Perfil/GamerCard → Jogos → Forza → lista → detalhes da 155 → voltar.
Uma pasta isolada nova não contém esse histórico; não recriar o desbloqueio.
Nome, pontos, descrição, imagem disponível, estados e retorno devem receber
aceite pelo controle físico. Integração durante gameplay fica para execução
manual do mantenedor, sem lançamento automático pelo agente.

A conquista 155 existente não foi apagada, recriada ou reenviada. Não há suporte
novo anunciado para quatro usuários simultâneos, serviços Live ou seleção
completa de avatar/gamerpic. Sem push, tag, Release ou commit novo.

### Retomada em 22/10: resultado manual e atalho direto do Guide

Run `20261022T021930Z-session-0-dash_17559` (aplicativo principal, sem
sessões concorrentes): duas criações por **Entrar → Criar perfil** concluíram
com logon (`E00B5870B522703B` e `E00CC470A0C7DF85`), o Guide foi restaurado
(`21015 -> 0`) e a dashboard continuou aberta até o mantenedor fechar a janela
(`session window closed by user`, 111.701 ms, sem exceção capturada, PPC vazio).
Isso confirma a rota Entrar → Criar perfil no build `69e532…/5898c6…`; não
certifica as demais telas de perfil nem a de conquistas. O run seguinte,
`20261022T022243Z`, usou o renderer Native e terminou com `exit 78` (PM4
`0x65` não suportado); pertence ao G1, não a perfis.

O atalho direto **Criar perfil** do Guide não usa `XamShowCreateProfileUI`.
O handler original do HUD (`913E85E0`, modo 2) chama `913EA808`:
`XamAppRequestLoad()` e, se não zero, `XamAppLoad("createprofile.xex",
[this+72], 0, 0, 0, 0)` (string em `913E1AF4`). Os dois exports eram
`REX_EXPORT_STUB`. Os logs registram exatamente esses dois stubs no Guide
aberto: `20261022T012207Z` às 22:22:29.600, `20261022T000450Z` às 21:05:33,
`20261021T114117Z` e `20261022T021536Z`. Nenhum pedido `2100E` foi emitido,
o que explica "não chega a criar".

No XAM original, `XamAppRequestLoad` (`816CAD70`) é o gate `816CAC60(0)`, o
mesmo que `XamShowCreateProfileUIEx` (`816C3540`) consulta antes do FE/2100E.
`XamAppLoad` (`816CDD58`) guarda nome (260), buffer (até 256 bytes) e inicia o
app; sem host devolve `E_FAIL` (`816CDF7C`). O CreateProfile original
(`90107828`) copia os 136 bytes do pedido **somente se o ponteiro existir**
(`90107850`), tratando pedido nulo explicitamente.

Patch `0131-original-app-load.patch` implementa os dois exports por um hook
do perfil; o host 17559 inicia `createprofile.xex` pelo mesmo caminho do app
original, com pedido nulo e o controle recebido. Buffers e OVERLAPPED/handle
são recusados (`E_NOTIMPL`), sem fingir carga; módulos desconhecidos
devolvem `X_E_NOTFOUND`, sem fallback para outro app. Hipótese explícita:
o XAM passa pedido nulo nesse caminho (`816BCE74` não rastreado por completo).
A fila, a suspensão do Guide e a restauração preservam o controle do app sem
pedido.

Validação (pasta `.local/evidence/validation/profile-p0-app-load-20261022/`):
o primeiro build recusou corretamente o SDK alterado antes do registro no
lock; após registrar 0131 (131 patches, `apply-sdk-patches.ps1 -Check` sem
divergência), `app-load.log`: **40 asserções / 2 casos**; `related.log`
(perfis, Sign-in, conquistas, Guide, Storage, HUD): **1.977 asserções /
38 casos**. Não há red executável do teste novo: ele não compila sem o hook;
o red é o stub registrado nos runs manuais acima. Aceite visual do atalho
direto pendente de teste manual.

Preparo oficial `build-title.ps1 -Title dash_17559` (reconstrução segura, sem
estado incremental autenticado): todas as etapas `exit 0`, executável às 23:47
(horário local de 21/10). Recibo `dash_17559.exe.preparation.json` com
**13 artefatos e 4.032 entradas**; os 13 hashes conferidos independentemente,
incluindo `rexruntime.dll` `4a95696a…`, o patch 0131, `xam_misc.cpp` e o host
`system_ui.cpp`. Não houve execução da dashboard pelo agente.

Teste manual pendente, com captura de pilha ativa em Diagnóstico: abrir a
Retail 17559 pelo aplicativo, abrir o Guide sem perfil conectado, escolher o
atalho **Criar perfil** e concluir; depois cancelar uma segunda tentativa.
O log deve mostrar `original application request 0002100E size=0 (XamAppLoad)`.

**Aceite manual do atalho direto (22/10).** Run
`20261022T025119Z-session-0-dash_17559`, runtime `4a95696a…` (produto acima):
Guide → `0002100E size=0 (XamAppLoad)` → Guide suspenso → CreateProfile
original → logon `E003C1ED462FF1C3` (flags 24) → app concluído →
Guide restaurado (`21015 -> 0`). O mantenedor confirmou que funcionou; a
janela foi fechada por ele após 62 s, sem exceção capturada (PPC vazio).
Restaurar o CreateProfile externo devolveu `80004004` (E_ABORT), o mesmo
comportamento da rota Entrar → Criar perfil.

### Tela de conquistas: roteamento do Guide (22/10)

Análise estática dos imports de `hud.xex`/`gamerprofile.xex`/`signin.xex`
contra os stubs efetivos (stubs do SDK menos os imports encaminhados ao XAM
original em `system_ui_imports.cpp`), cruzada com os stubs de fato atingidos
em todos os runs 17559. Defeito comprovado: o Guide (`913E6B88`) chama
`XamUserIsPartialProfile(controle)` e, se não zero, abre as conquistas do
**título atual**; se zero, a lista de **Jogos** (`XamShowGamesUI`). O stub
devolvia o índice do controle: só o controle 0 chegava à lista de Jogos.
Patch 0132 (ver README dos patches) devolve 0, derivado do original
`816DAAC8` (bit `0x80000000` dos flags da conta; contas locais sem flags).
`[partial-profile]` red 5/10 falhas → green 10/10; relacionados
(perfis, Sign-in, conquistas, Guide, Storage, HUD, conquistas offline):
**2.433 asserções / 53 casos**, um skip esperado (teste multiprocesso que
exige `tests/local/offline-achievements-process.ps1`). Preparo oficial:
13 artefatos/4.033 entradas conferidos, runtime `4bb5a0fe…`.

Ainda observado e não corrigido: o Sign-in chama `XamReadImage` (stub)
centenas de vezes por abertura (`9010CFE4`/`9010FAB0` → original
`81691EE8 → 81691C50`, cache de imagens `81A9DE10`); as imagens de jogador
da lista provavelmente não aparecem. Aceite visual da tela de conquistas
(Guide → Perfil → Jogos → Forza → conquista 155 → voltar) pendente.

### Conquistas de um jogo carregando sem parar (22/10)

Relato manual: a lista de Jogos aparece; ao abrir o jogo, a tela fica
carregando. Run `20261022T030932Z-session-0-dash_17559` (perfil Forza,
`E00C0DA32FA8F38D`): `804_LoadAchievements` → `802_Achievements` e depois
nenhum log por ~6 s até o retorno. O par `XamFormatMessage`/`XamReadImage`
repetido a cada quadro pertence à lista de Jogos após o retorno.

Investigação, em ordem:
1. Hipótese de textos acima de 464 bytes **refutada**: XDBF reais medidos
   (FM4 `4D5309C9`: 50 conquistas, máx. 170 caracteres em pt-BR).
2. Sonda CPU nova `--achievements-real-copy` (cópia somente leitura de
   `profiles/` e `offline-achievements-v1/` numa pasta temporária; hashes
   dos 15 arquivos reais iguais antes/depois): com flags 7 as operações
   originais `90120CD0`/`901200D8` funcionavam — logo, a tela usava outros
   parâmetros.
3. RE do GamerProfile: conclusão chega por mensagem XUI `0x0800000B` de um
   gerenciador de operações com timer (`90124468`/`90124718`); `90106F78`
   esconde o indicador antes de tratar sucesso ou erro. A
   `LoadAchievementsScene` (`XM_INIT` `90117710`) configura a operação de
   conquistas com **flags `0x27`** e quantidade 150 (`901200C0`).
4. O SDK recusava `flags & ~7` com 87; o original `81670190` não valida
   flags. Red `87 == 0` → patch 0134 → green; sonda com dados reais e
   `0x27`/150: 50 conquistas, 155 desbloqueada.

Patch 0133 acrescenta log `info` de baixa frequência nos enumeradores de
conquistas/títulos, útil caso a tela ainda não carregue. Relacionados:
**2.264 asserções / 49 casos** (um skip esperado). Aceite visual pendente.

Preparo oficial com 0133/0134: 13 artefatos/4.035 entradas conferidos, runtime
`4e5bcb84…`.

**Aceite manual da tela de conquistas (22/10).** Run
`20261022T034550Z-session-0-dash_17559`, runtime `4e5bcb84…`: o mantenedor
confirmou que as conquistas do Forza abriram. Log do 0133: enumerador
`title=4D5309C9 flags=27 count=150 -> 0`, página `B000A ... items=50`. A
lista passou a pedir os ícones por `XamReadGameTileImage`, ainda stub: os
ícones das conquistas não são carregados. Também observado no início da
sessão: `XamEnumerate` do enumerador de títulos com `flags=2` recusado pelo
SDK (o XAM original usa o bit 1 para despacho direto, `816F1134`); a lista
seguinte, sem flags, funcionou. Ambos ficam como pendências separadas.
