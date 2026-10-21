# Conquistas offline — Retail 17559

Base solicitada e verificada: `main`, `2ff9cf49f526b1ac3e726b97b95f473ae2f69f54`.
Implementação de 21/10/2026. **Aceite funcional pendente do mantenedor.**
Nenhum jogo ou dashboard foi executado pelo agente; sem Computer Use, alteração
de originais, commit, push, tag ou Release. Retail 1888 ficou fora do trabalho.

## Contratos e armazenamento

Os patches 0106–0111 mantêm o SDK no pin `f5337cdc`. A escrita original
`XUserWriteAchievements` chega ao XGI `0xFB`, mensagem `0xB0008`: cabeçalho
de 8 bytes `{count, pointer}`, seguido de pares `{user_index, achievement_id}`.
O usuário de cada entrada deixa de ser ignorado. O runtime resolve o XUID local
ativo e o Title ID do executável, valida o lote completo e só notifica após a
gravação durável. Um ID inexistente, perfil ausente, buffer inválido ou falha de
disco retorna erro; repetir uma conquista já gravada não soma pontos nem notifica.

`XamUserCreateAchievementEnumerator` usa o Title ID solicitado, XUID de 64 bits,
offset e limite de registros. Title ID zero significa o executável corrente.
Os registros PPC têm 36 bytes, ou 500 com o espaço de strings UTF-16 BE.
O estado offline usa `ACHIEVED` (`0x20000`), sem `ACHIEVED_ONLINE`.
`XamEnumerate` verifica capacidade antes de consumir a página. Enumeração de
títulos jogados, contadores globais e por título e leitura de imagens usam o
mesmo armazenamento. Os caminhos síncronos e OVERLAPPED preservam o resultado
real do serviço.

Para FB/`0xB0008`, `XMsgStartIORequest/Ex` retorna HRESULT `S_OK` quando aceita
a solicitação. O resultado do serviço fica em LastError no modo síncrono;
no OVERLAPPED, `result` é 0/`ERROR_FUNCTION_FAILED` (1627), `extended_error`
contém o HRESULT real e `length` é zero. O wrapper `XUserWriteAchievements`
identificado no Forza (`82A6E5F8`) converte a aceitação para
`ERROR_IO_PENDING` (997) com OVERLAPPED e 0/1627 sem ele. Os testes chamam
XMsg diretamente e verificam essa diferença de camadas. A correção de XMsg
fica restrita a essa mensagem; os demais serviços mantêm o comportamento anterior.

Metadados vêm exclusivamente do recurso XDBF original do executável identificado:
IDs, nomes, descrições, pontos e imagens. O recurso bruto conserva seus idiomas
e imagens para consulta pela dashboard depois de encerrar o jogo. Overrides
editáveis `achievements.toml` e desbloqueios globais antigos não são fonte de
progresso local. O parser valida tamanhos, contagens, offsets e tabelas antes
de publicar ponteiros para os recursos.

O estado fica em `offline-achievements-v1` abaixo da raiz de perfis da sessão:

- `metadata/<TitleID>.xdbf`: recurso original encapsulado com identidade e checksum;
- `profiles/<XUID>.dat`: registros ordenados por Title ID + Achievement ID e FILETIME;
- `store.lock`: trava do sistema operacional entre processos.

A troca da raiz de perfis no handoff também troca o serviço de conquistas e
importa o recurso do jogo nessa raiz. Os arquivos de perfis C7, saves C8 e os
arquivos antigos de conquistas são preservados. Não se atribuem desbloqueios
globais antigos a um XUID por suposição.

A escrita usa staging, flush durável e substituição atômica, sem remover antes
o destino válido. A trava espera no máximo um segundo e retorna erro explícito
se não puder prosseguir. Checksums XXH3 detectam corrupção acidental; não são
autenticação criptográfica. Metadados atualizados podem acrescentar IDs/idiomas/
imagens se preservarem os IDs e pontos anteriores; um conflito é recusado e não
autoriza novos desbloqueios com o catálogo antigo.

## Interface original da 17559

As solicitações de conquistas usam GamerProfile original, FE/`0x21008`, payload
de 544 bytes, modo 2 (lista) ou 4 (detalhe), com Title ID e XUID completos.
As imagens são lidas do XDBF; o caminho de textura decodifica pixels reais.
O decoder compartilhado suporta PNG; os demais formatos continuam preservados
como bytes originais, sem textura substituta. `XamGetCachedTitleName/Ex` consulta
o mesmo cache de recursos para o cabeçalho do GamerProfile: r5 informa capacidade
em WCHARs, r6 contém flags no Ex, e o resultado disponível é síncrono.

Após um desbloqueio novo, a ponte guarda XUID, Title ID e Achievement ID, monta
o texto `pontos|nome` e entrega FE/`0x21013` ao apresentador original. O formato,
tipo 27 e prioridade 2 foram identificados no XAM 17559 (`8190FBE8`, string
UTF-16 em `8162F678`). O apresentador original (`816AB9C0`, inicialização DC
`81692978`, despacho `816AC0A8`) usa `notify.xur` e o compositor `816CCF00` já
utilizado pelo Guide. Os callbacks constam no registro AOT existente.

A fila é drenada no `VdSwap`, vinculada ao XUID que ganhou a conquista. A
notificação é não modal: não abre o Guide nem captura os controles do jogo.
Sem eventos pendentes não há alocação ou chamada guest adicional para a fila.
Nenhuma notificação é desenhada pelo launcher ou pelo overlay de conquistas do SDK.

Isso é evidência de implementação e análise estática. Aparição, animação, som,
expiração e os valores visualmente apresentados nas telas originais precisam do
aceite manual; não foram observados nesta tarefa.

## Recursos locais e jogo escolhido

Leitura dos XEX em modo ferramenta, com armazenamento de teste isolado e sem
iniciar threads guest:

| Jogo | Title ID | Conquistas no XDBF | Pontos do catálogo | Ícones de conquistas |
|---|---|---:|---:|---:|
| Forza Horizon | `4D5309C9` | 50 | 1000 | 50 |
| Castle Crashers | `584108B7` | 12 | 200 | 12 |
| Motocross Madness | `58410B0F` | 30 | 400 | 30 |

Esses totais são metadados, não desbloqueios. Cada extração também encontrou o
ícone do jogo. Evidências locais: `.local/evidence/validation/offline-achievements-20261021/`.

**Candidato ao aceite: Forza Horizon**, XEX SHA-256
`db40df605ade49a612b35a7a24c38f6004bcb17a88ed6b48288de16df9e3987c`.
O recurso em português identifica `155`, **Bem-Vindo Ao Horizon**, **10 G**,
com requisito “Chegue ao Festival Horizon.” A escolha aproveita o percurso
inicial e a execução histórica já documentada do título; não afirma que esse
desbloqueio foi observado agora. Se o save já passou desse ponto, o mantenedor
deve escolher outra conquista ainda não obtida, sem apagar ou editar seu save.
Castle Crashers é Trial no acervo e não é a referência principal para este aceite.

## Validação técnica

Testes sintéticos utilizam somente diretórios próprios; não preenchem o critério
de uma conquista emitida durante gameplay real. Não foi executada suíte completa.

Comandos usados: `scripts/devshell.cmd cmake --build --preset win-amd64-release
--target x360n_sdk_runtime_checks`; execução do binário Release com filtros
`[offline-achievements],[offline-achievement-api],[achievement-ui],[xdbf]`;
regressão focada `[gamercard],[profile-sizing],[local-profile],[save-storage],
[profile-ui],[guide-context]`; script `tests/local/offline-achievements-process.ps1`.
Cada execução de testes teve limite de 120 s; os dois processos de persistência,
20 s cada. A preparação dos títulos usa `scripts/build-title.ps1`, paralelismo 4
por título e limite de 20 minutos, sem iniciar os executáveis produzidos.

Resultados já consolidados:

- Novos serviços, parser e UI: **1.263 asserções aprovadas em 19 casos**;
  o caso de processos é pulado nessa invocação e executado separadamente abaixo.
- C7/C8/GamerCard/UI: 909 asserções em 25 casos aprovados.
- Persistência entre dois processos: 6 asserções na escrita e 17 na reabertura,
  com dois perfis e dois títulos; hashes dos arquivos permaneceram iguais na leitura.
- Metadados originais por `Runtime` em modo ferramenta, sem execução guest:
  Forza 664, Castle 170 e Motocross 404 asserções aprovadas. Verificam IDs, nomes,
  descrições, pontos, imagens byte a byte, zero desbloqueios e cache após reinício.
- Sonda opcional de 100 consultas `List`, mesmo processo e disco local:
  Forza 7,529 ms/chamada, Castle 6,754 ms/chamada, Motocross 7,608 ms/chamada.
  Não é medição de FPS nem demonstra ganho: não existia operação anterior com
  a mesma persistência/validação para comparação equivalente. A consulta faz
  leitura e validação de disco; fica fora do caminho por frame sem eventos.

Falhas encontradas e corrigidas durante a integração:

- Build inicial: uso de accessor inexistente, tipo `UserModule` incompleto e
  macro de erro qualificada indevidamente. Corrigidos antes de executar testes.
- Fixture de textura BMP incompatível com o decoder PNG do SDK. Substituída
  por PNG válido de 1×1; a asserção verifica o pixel decodificado real.
- XDBF original: versão global `0x00010000` e tamanhos das seções excluindo
  os quatro bytes da assinatura. A fixture inicial não representava esses
  campos; parser e fixtures corrigidos contra o recurso original do Forza.
- Revisão de exports: os wrappers raw da UI precisavam de registro no resolver.
  O 0110 registra os sete exports e os testes executam suas primeiras chamadas
  semânticas pelo ponteiro resolvido por ordinal, verificando os resultados.
- Preparação dos títulos: tentativas no sandbox retiveram processos após o
  Ninja terminar. As árvores antigas identificadas desta tarefa foram encerradas
  e o driver oficial foi retomado fora do sandbox, com a aprovação automática
  da ferramenta, sem alterar a infraestrutura de build. Uma consulta diagnóstica
  com versão diferente de Ninja invalidou o log incremental da 17559; a preparação
  posterior faz reconstrução segura e não reaproveita esse estado como certificado.

A primeira reprodução independente aplicou os **109 patches**, em ordem, desde o pin
e comparou os **18 arquivos** atingidos por 0106–0110: todos coincidem byte a
byte com o checkout e com os hashes do lock. Evidência dessa etapa, anterior ao 0111:
`sdk-chain/final-exports/`; as duas verificações anteriores foram marcadas como
superadas. SHA-256 do lock daquela etapa:
`724c2e67dc91dfa0339655b8b4dfecd167e0059a2cee1cf7f42687484ec9ed32`.
Os três XEX originais mantiveram seus hashes após a leitura.

Binário Release dos testes finais de contratos, regressão e reinício, SHA-256:
`f927e022bd3eef3e006030443f9556acbb79c5ccd478a3d32b4583c07c3ace89`;
runtime dessas execuções:
`984d7e02c364db2d25bf0532e6d7b55000d69270130d641826179f466e25ca16`.
Os ensaios anteriores de metadados têm seus próprios hashes nos manifests
`metadata-forza-parser.json`, `metadata-castle.json` e `metadata-moto.json`;
os ajustes posteriores de exports/UI não alteraram o parser ou o armazenamento.

As preparações finais de ambos os títulos terminaram com código 0, dentro do
limite de 20 minutos. O driver oficial verificou comandos de compilação Release
antes de publicar os recibos. A conferência posterior aprovou identidade,
hashes de entradas, artefatos, produtor e estado incremental, além dos 106 globs
de cada título. Não foi necessário executar nenhum produto para essa verificação.

| Produto | Entradas verificadas | Artefatos verificados | Conclusão em 21/10 (UTC−3) |
|---|---:|---:|---|
| Forza Horizon | 4.072 | 6 | 06:01:46 |
| Retail 17559 | 3.983 | 12 | 06:01:51 |

Manifestos: `forza_horizon-artifacts-final.json` e
`dash_17559-artifacts-final.json`, na pasta local de evidências. Os logs do driver
estão nos diretórios `.local/build/titles/<título>/`, com os identificadores
`d40c574e17f4453c82dd180fd561bc70` (Forza) e
`65cd5883356349f3a900dc40bae7504b` (17559).

SHA-256 dos produtos preparados antes do 0111 (superados pela atualização abaixo):

| Arquivo | SHA-256 |
|---|---|
| `forza_horizon.exe` | `7520e32d60f2bcd5d490e70ee9b54aebcb068c663f357ca1ad972ea257c2b8fe` |
| Forza / `rexruntime.dll` | `6a7f4671aee19c271578d51c280230b4610b6e5a7c4ea2dd1484c5df6e5af28b` |
| `dash_17559.exe` | `86a86161a6d1daf13935ac66103010c1b12253b6d37b1d57d8ae77c9fdebf915` |
| `dash_17559_session_ui.dll` | `87029b2d1e4a05ea0321a458d5684b240df25c8422e69c9ced23aeb27c7ee546` |
| 17559 / `rexruntime.dll` | `bccd3ecc233bbe5ef295c39e5277db84a36c8f1dc655b5c5fe0f0401898caf31` |

Recibos: Forza `ab5c8f8dcb0b0f2b72b862bd177144ea80cb6beef4864cb985f4d13263943ae2`;
17559 `b6a2ee571ac680122a49189f8a79376ba9023a2d129e5a1541664c28d34c1906`.

Logs e manifests locais ficam em
`.local/evidence/validation/offline-achievements-20261021/`.
Os checksums detectam corrupção, mas esta rodada não simula queda de energia
física. Também não certifica Linux/macOS, execução de dois usuários simultâneos,
Xbox Live, formatos de textura além de PNG, animação/som ou o desbloqueio em gameplay.

## Aceite manual obrigatório

Seguimento de 21/10: após o mantenedor informar que o Forza não aparecia no
launcher, confirmou-se catálogo vazio. A importação anterior havia sido impedida
pela trava dos builds. A nova importação revelou dois arquivos adicionais de
cache gerados pelo codegen dentro da pasta original; isso selecionava um alvo
genérico em vez da receita preparada. Os arquivos originais não mudaram, mas o
inventário da pasta havia sido contaminado pela ferramenta. O cache do Forza,
o equivalente da 17559 e o primeiro registro de importação foram preservados em
`codegen-cache-quarantine/`, fora das entradas originais.

O patch 0111 impede essa escrita em modo ferramenta com raiz de usuário implícita
igual à entrada. O teste usa uma cópia temporária do XEX e verifica ausência do
cache; o teste com raiz explícita verifica que a persistência continua funcionando.
Passaram 668 asserções em dois casos Release, sem gameplay (`tool-input-isolation.log`).
A importação final verificou os 2.432 arquivos originais e selecionou o alvo
`forza_horizon`, `4D5309C9`, `complete=true`, em 70,6 s: código 0 e seis asserções.
Log: `.local/tmp/import-check-20261021/forza-import-clean-catalog.txt`.
O launcher já aberto precisa ser reaberto para carregar esse cadastro.

A reprodução atual aplicou **110 patches** e conferiu os mesmos **18 arquivos**
contra SDK e lock, mantendo o pin: `sdk-chain/tool-input-isolation/report.json`.
Lock atual SHA-256:
`5f5026d32b8fd4a64972bbab56346dbc17ca2e98aebc794c576076818975dc77`.
O teste de isolamento e persistência usou o binário Release SHA-256
`1612c766ebe85b0690ea98cf5f379633f97c7a3506dd5f9c6702d842701761bb`
e runtime `106c0145925aa54e43a616ef6d5617cb407c7a55aeab4deb369b90a8063db720`.

Ambos os produtos foram reconstruídos com o 0111 pelo driver oficial, código 0,
em menos de 20 minutos cada, e seus novos recibos passaram em todos os checks:
17559 com 3.984 entradas/12 artefatos, Forza com 4.073 entradas/6 artefatos,
106 globs por título. Os manifests `dash_17559-artifacts-final.json` e
`forza_horizon-artifacts-final.json` agora descrevem estes produtos atuais.
Logs: `e3cdc54cbf34450b88f962af728b881d` (17559) e
`77037c6650064e00b1117f4d047c09a4` (Forza). A conferência após codegen confirmou
inventário idêntico ao catálogo e ausência de cache nas duas entradas originais
(`catalog-registration-final.json`).

| Produto atual | SHA-256 |
|---|---|
| `forza_horizon.exe` | `4aae81f83311ef49e0bfde5b5ab56aba09d9d4fe8e8926e15e087e7f943b2764` |
| `dash_17559.exe` | `9ea715534e095102df5571e162f55f30eb5cdea2a50162bcd24392aff2d1ae46` |
| `dash_17559_session_ui.dll` | `0aa3c4771b5a8163fb0fc59e0c47ce7fadd2616235266730564a83e3abaec0e1` |

Recibos atuais: Forza
`e2376dc5ce95a361351d304cecf59007df3ef22fd14f372c4fdea7091cdeff2b`;
17559 `830a4fc697f52b0099b9c2aa382cbd6b45eb22bf28a669f52e169af3c0dcc75d`.

Usar os produtos Release reconstruídos de Retail 17559 e Forza Horizon, pela
sessão normal do launcher/My Games. Se o jogo não estiver na biblioteca, usar o
fluxo existente de importação; a tarefa não adiciona títulos fixos ao catálogo.

1. Entrar no Forza com seu perfil local, conferindo o nome na interface original.
2. Obter uma conquista real durante gameplay; preferencialmente chegar ao festival
   para `155 / Bem-Vindo Ao Horizon / 10 G`, se ainda elegível nesse save.
3. Confirmar a notificação original com nome e pontuação, e sua expiração normal.
4. Abrir o Guide e as conquistas/GamerCard; conferir incremento de 10 G e uma
   conquista (ou os valores reais da conquista escolhida), sem duplicação.
5. Encerrar e reabrir a sessão, confirmar persistência e conferir que outro
   perfil não herdou esse desbloqueio. Preservar os controles já aprovados.

Em caso de falha, guardar o log da sessão e o percurso executado; isso exige
continuação da correção, não aprovação por testes sintéticos. Somente após
aprovação explícita do mantenedor: commit Conventional Commits e push na `main`.
Sem tag ou Release.

Durante a validação apareceram alterações concorrentes em
`src/desktop/model/imported_games.cpp`, `tests/desktop/imported_games_test.cpp`
e `docs/reports/import-path-loop-2026-10-21.md`. Elas não pertencem a esta
implementação e não foram editadas ou revertidas por este trabalho. O commit
posterior ao aceite deve incluir somente o escopo de conquistas, preservando
também eventuais hunks compartilhados de documentação da outra tarefa.

## Referências

- Código do SDK no pin e patches locais indicados em `deps.lock.json`.
- Originais locais 17559 identificados pelo perfil de preparação; nenhum módulo
  de outra dashboard foi utilizado.
- [Xenia Canary: contratos XAM de usuário](https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/kernel/xam/xam_user.cc),
  referência primária de pesquisa, confrontada com o código AOT original local.
- [Guide e compositor original](graphics/guide-composition-2026-10-16.md).
