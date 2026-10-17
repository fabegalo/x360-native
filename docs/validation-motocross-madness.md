# Validação experimental: Motocross Madness (XBLA)

Atualização: serviço opt-in, tutorial e integração na main foram demonstrados
em [Avatares §§14–15](avatar-system.md#15-integração-na-main-2026-10-13).
Chão preto e piloto deformado foram corrigidos em 16/10 (seções abaixo); save/load continua pendente. O diagnóstico
abaixo preserva os bloqueios anteriores a essa integração.

## Correções e diagnóstico de 16/10/2026

SDK/LLVM continuam pinados. O usuário autorizou reiniciar o jogo e depois
revogou Computer Use; a partir disso, trabalho por código, terminal e logs.
Perfis, saves, módulos originais e isolamento 17559 preservados.

- **0082 — consulta de tamanho antes do sign-in:** a inicialização de voz
  consultava `XamUserReadProfileSettingsEx` com usuário 255 e buffer nulo.
  O retorno 1245 ocorria antes de informar o tamanho; o chamador propagava
  E_FAIL e chegava a um acesso nulo. Agora a consulta informa o tamanho e
  retorna 122; a leitura efetiva continua exigindo usuário autenticado.
  Os dois ABIs, ponteiros inválidos, buffer intocado e leitura autenticada
  foram verificados: red com 10 falhas; green com 101 asserções em 3 casos
  (`[profile-sizing],[gamercard]`). Evidência local:
  `.local/validation/motocross-current/profile-fix/`.
- **0083 — transação física atômica:** em host com páginas de 4 KiB, a alocação
  podia alterar pai/alias sem a trava global; Release/Decommit/Protect também
  transferiam a guarda para o callback e prosseguiam depois de soltá-la.
  O patch mantém a trava até o fim da operação. O teste anterior permitiu
  alocação nas três regiões enquanto outra thread mantinha a trava (3 falhas);
  após o patch, 13 asserções/2 casos passaram, incluindo 2.000 ciclos de
  alocação/liberação concorrentes e contagem de páginas. `memory-red.log`,
  `memory-green.log` e `memory-apply.log` na mesma raiz de evidência.
  O log anterior registrou liberações com metadados vazios no alias e
  alocação existente no pai. A correção elimina a janela demonstrada;
  não certifica todas as causas possíveis de liberações inválidas.
- **Native, teste solicitado:** a sessão
  `.local/runs/20261016T084216Z-motocross-native-quick/` saiu com código 78
  após 39.114 ms: `mip-only texture not implemented`. O carregador Native
  agora omite o nível base ausente, mantém os números/offsets dos mipmaps,
  evita ler/proteger o endereço zero e respeita o LOD mínimo já calculado.
  Red: 2 falhas; green: 712 asserções em 2 casos. Comparação CPU/GPU com
  RGBA8/BC1, layout linear/tiled, mip tail compactado ou separado, cubos e
  volumes. Evidência `native-mips-{red,green}.log`. A sessão seguinte
  `.local/runs/20261016T084616Z-motocross-native-play/` terminou pela janela
  após 60.480 ms, exit 0, 813 presents; não repetiu a falha mip-only.
  Não houve inspeção visual nessa sessão nem comparação de FPS equivalente.
  Esse ajuste pontual não retoma o roadmap Native suspenso.
- **0084 — consulta XCTD:** implementa a classe 27 de `NtQueryInformationFile`
  para os arquivos brutos apresentados pelo VFS. Bit 0 identifica a assinatura
  big-endian `0x0FF512ED`; a biblioteca original do jogo usa esse bit e, quando
  a consulta falha, compara a mesma assinatura por leitura própria
  (`82AB1FC8`, `82AB2030`). A implementação lê quatro bytes no backend com offset
  explícito, sem alterar o cursor guest nem sinalizar eventos/portas de I/O.
  Não usa o atributo de compressão do filesystem host nem descomprime o arquivo.
  Leitura negada mantém seu erro; EOF curto indica ausência da assinatura.
  Red: 18 falhas/81 verificações; green: 81 verificações/1 caso.
  Teste sintético cobre arquivos comuns, assinatura XCTD, endian invertido,
  vazio/truncado, cursor preservado, limite da saída, handle inválido, buffer
  curto e ausência de permissão de leitura. Evidências `xctd-{red,green,apply}.log`.
- **0085 — máscara de usuários ociosos do microfone:** o stub devolvia o próprio
  argumento em r3. O original 17559 (`816FB9C0`, export `48C`) guarda todos os
  32 bits, enfileira um DPC quando r4 não é zero e retorna zero. O runtime agora
  conserva a máscara e uma atualização pendente coalescida por KernelState,
  independente da presença de hardware. Não há backend de captura nem consumidor
  de áudio para essa atualização; isso não implementa microfone. Red: 3 falhas
  de retorno; green: 15 asserções, incluindo coalescência e atualização sem aviso.
  Evidências `mic-{red,green,apply}.log` na raiz local acima.
- **0086 — propriedade da reserva de imagem XEX:** `ReadImage` chamava Reset
  no heap inteiro em cada carga/tentativa de chave, apagando metadados de módulos
  vizinhos e causando liberações inválidas no encerramento do codegen. Agora
  cada XEX registra sua reserva, libera apenas essa reserva ao tentar outra chave
  e só libera no Unload se houve alocação. Reserva e commit separados recusam
  sobreposição, pois o AllocFixed combinado legado permite páginas ocupadas.
  Um XEX sintético reproduziu 12 falhas/47 asserções: sucesso, PE inválido com
  retry, colisão com conteúdo existente, integridade dos vizinhos e contagem
  de páginas. Uma verificação adicional reproduziu a reserva retida quando
  ambas as chaves falham; o caminho agora chama Unload antes de devolver falha.
  Green final: 48 verificações novas e 103 de XEX/imports/codegen/recursos,
  151 asserções em 5 casos. Evidências `xex-{red,green,regression,apply}.log`,
  `xex-failure-red.log` e `xex-final-green.log`. O link final usou o mesmo response
  file/objetos do CMake, via devshell, com saída `motocross_log_checks.exe`, porque
  outra execução `[games-played],[gamercard],[title-return]` mantinha o executável
  padrão aberto. O outro processo não foi interrompido; as tentativas de link
  bloqueadas não foram tratadas como builds aprovados.
  Sem afirmação de ganho de FPS ou correção das fetch constants.

O usuário pediu retorno ao Xenos. O teste com runtime corrigido ainda registra
fetch constants de tipo 0; a correção de memória não resolveu esses avisos.
Instrumentação local da rotina de binding mostrou cabeçalhos e cache de
texturas válidos nas amostras com objeto vinculado; a investigação de origem
continua. Não foi ativado `gpu_allow_invalid_fetch_constants`, nem alterado
o comportamento guest para forçar tipos válidos.

As amostras locais anteriores (`diagnostic-build/sample-*.{regs,vs,ps,txt}`)
distinguem usos condicionais de usos ativos: em sample-1, as constantes do PS
provam inativos os fetches 3/4/5/6, mas o fetch 8 é incondicional. Em sample-10,
há 16 fetches incondicionais do slot 0. A rotina guest `8297DCF0` limpa o tipo
ao desvincular um objeto nulo e mantém os demais campos; `8298B0A8` submete o
cache usando máscara de 64 bits. As amostras não demonstraram um objeto não
nulo com tipo inválido no cache. Portanto, ignorar todas as mensagens como
predicação inativa ou reativar descritores antigos não tem justificativa.
Não foram realizadas novas capturas nem testes visuais nesta análise dos logs.

Depois o usuário restringiu o escopo aos erros dos logs. O teste gráfico
diagnóstico foi encerrado e sua DLL não foi incorporada ao build normal.
A sessão `20261016T090406Z-motocross-xenos-logs` usa Xenos normal, sem hooks de
texturas nem capturas da GPU. Os avisos de arquivos ausentes e do stub de voz
continuam explícitos; não foram fabricados recursos ou retornos de sucesso.

O build final com 0082–0084 passou pelo driver `build-title.ps1`, incluindo
codegen/recompilação e recibo de preparação renovado. A sessão final
`20261016T092235Z-motocross-xenos-logs-fixed` usa o executável normal em
`.local/build/titles/motocross_madness`, Xenos e limite de quatro horas.
Nos primeiros 58 segundos: zero erros XCTD, zero `BaseHeap::Release failed`,
zero críticas; continuam dois erros de resolução de caminhos (`devkit:` e
`media:`), quatro stubs de voz e 12.616 avisos de fetch de textura. Não é um
log limpo. Snapshot preservado em `final-startup.log` e contagem em
`final-log-summary.json`, sob a raiz de evidência local acima. Processo 3160
confirmado ativo/respondendo; nenhum teste visual nesta verificação final.

O codegen terminou com exit 0, mas seu log separado ainda contém
`BaseHeap::Release failed because address is not a region start`; portanto,
0083 não deve ser descrito como correção de todas as causas desse erro.
Suítes completas e regressões de outros títulos não foram executadas.

Após 0085/0086, o codegen de produção terminou com exit 0 em 58 s, sem
`BaseHeap::Release failed`, `PhysicalHeap::Release failed` ou marcadores de erro.
Log preservado em `.local/validation/motocross-current/xex-codegen-final.log`.
A sessão anterior `20261016T092235Z-motocross-xenos-logs-fixed` havia encerrado
normalmente (exit 0, 112.239 ms); não permaneceu aberta durante essa correção.

O build final passou por todas as etapas do driver, com recibo renovado às
06:57:36 e marcador de preparação incompleta removido. A sessão
`20261016T095800Z-motocross-logs-0086` iniciou o executável de produção às 06:58
no Xenos, com perfil/saves preservados e limite de quatro horas. Processo 40824
confirmado ativo/respondendo. No snapshot de 67,548 segundos de log: zero
críticas, zero erro XCTD, zero falhas de liberação de heap e zero stub
`XamVoiceSetMicArrayIdleUsers`; serviço de avatares READY. Permanecem dois erros
de volumes ausentes (`devkit:`/`media:`), 19.854 avisos de textura inválida e três
imports variáveis não implementados. As contagens entre sessões não constituem
benchmark nem comparação de cena equivalente. Evidências locais:
`after-0086-startup.log`, `after-0086-summary.json` e
`.local/logs/motocross-logs-final-production-build.log`. Jogo deixado aberto;
nenhuma interação de janela ou validação visual executada.

### Revisão independente e origem dos avisos restantes

Revisão de código dos patches 0082–0086 e do ajuste Native, sem nova sessão do
jogo. Fatos: o lock tem 85 patches e os SHA-256 de 0082–0086 conferem com os
arquivos; o build `win-amd64-debug` passou (verificação dos patches inclusa);
16 testes focados passaram: `native.texture` (inclui os casos mip-only), os
cinco testes novos/alterados de 0082–0086 e os de heap XEX, alocação física,
perfil local e gamercard vizinhos. Os testes red não foram repetidos; valem os
logs citados acima. O README de `patches/rexglue-sdk/` ganhou as entradas
0082–0086, que faltavam.

Classificação dos avisos ainda presentes (sessões diagnósticas
`20261016T1013*`–`T1022*`, DLL de rastreio fora do build normal):

- **`devkit:\senoverride\0x58410b0f\transport.ini`:** consulta de atributos
  feita pelo jogo (pilha a partir de `82611F40`); resultado `-1` e o jogo
  continua. Em console retail o dispositivo `devkit:` não existe. Fato:
  comportamento do original; nenhum dispositivo deve ser inventado.
- **`media:/Tutorial_2k/Egypt_2K_panorama/panorama_2_7.raw.tga`:** o
  carregador de texturas do jogo tenta `pmedia:` (pacote), `game:\pmedia`,
  `media:` e `game:\media`. O índice do próprio pacote, lido na memória do jogo
  (9.515 de 9.515 nós), só contém os panoramas 12_9, 15_5, 17_0 e 8_2; o 2_7
  não está nos dados fornecidos. Fato: o recurso falta no conteúdo original;
  o jogo recebe objeto nulo e segue.
- **Três imports de variável (`XboxKrnlBaseVersion`, `StfsDeviceErrorEvent`,
  `UsbdDriverLoadRequiredEvent`):** o `default.xex` do jogo não importa esses
  ordinais (`x360n-content imports`); vêm do XAM Retail 17559 carregado na
  sessão. A decisão anterior de não preencher sem valor demonstrado
  ([dashboard §0.2](dashboard.md#02-imports-e-as-conferências-antes-da-execução)) continua válida.
- **Fetch constants de tipo 0:** o D3D do jogo zera o tipo ao desvincular
  (`8297DCF0`) e parte das amostras usa fetches inativos; outra parte usa
  fetches incondicionais. (Superado na seção seguinte: a compressão DXT1 do
  terreno usa tipo 0 de propósito em todos os draws.)

Conclusão: os erros de runtime demonstrados foram corrigidos; os avisos que
restam vêm do conteúdo original ou de uma questão de fidelidade em aberto.
Remover o restante do log exigiria mudar a política de logging do SDK
(severidade/deduplicação), não o comportamento guest. Hipótese não medida:
~300 avisos/s de fetch podem custar tempo na thread de renderização.
Riscos abertos: 0086 não foi exercitado na dashboard nem em outros títulos;
o custo do lock global extra de 0083 não foi medido.

## Chão preto: causa e correção (16/10/2026, tarde)

Sessões curtas autorizadas pelo usuário ("rode, tire suas conclusões"), Xenos,
serviço de avatar ligado, teclas/capturas só na janela do jogo. Diagnóstico com
hooks e uma DLL Xenos instrumentada em `.local/build/diagnostics/`, fora do build
normal; capturas e dumps na pasta temporária da sessão.

Cadeia demonstrada (fatos):

1. O chão próximo é o par `85473150CCAF3CD4/BE5335006ECA28C3` (pulá-lo remove o
   chão). Ele amostra, por tile, um mapa de cor DXT1 512×512 (`tf1`), detalhe de
   areia (`tf2`, correto) e uma máscara k_8.
2. O mapa de cor é comprimido em DXT1 **na GPU**: `E9B5E602…/B82D9F0C…` lê blocos
   4×4 da composição RGBA8 do terreno (dump correto, com pista e areia), escreve as
   quatro palavras de 16 bits do bloco como inteiros num RT `k_16_16_16_16`
   (`RB_COLOR_INFO 0x03650000`, exp_bias −10) e faz resolve para o mesmo formato
   como inteiro com sinal, exp_bias +10 (`RB_COPY_DEST_INFO 0x000A6D01`).
3. Essa passada lê `tf0` com tipo 0 em 100% dos draws (7.150/7.150), com
   descritores novos por nível (512/256/128). O hook em `8297DCF0` mostrou 260 mil
   vínculos de objetos, todos com cabeçalho tipo 2; os tipos 0 chegam por envio
   do próprio D3D do jogo (`8298B0A8`, chamadores `82985DD8/829859BC/82985438`).
   Sem `gpu_allow_invalid_fetch_constants`, a entrada vira zero e o chão fica preto.
   O Xenia Canary usa esse padrão desde `d36b1b38`; aqui ele foi aplicado só ao
   título, no manifesto. Hipótese apoiada pelo design do jogo: o hardware amostra
   texturas com tipo 0.
4. Com a entrada liberada, os blocos saíam `0000`/`FFFF` (readback do resolve):
   os shaders de resolve "full" não implementam destino inteiro, e o RT SNORM16
   com divisão por 32 mudava 32.702 de 65.535 inteiros. Patch **0087** corrige os
   dois no caminho D3D12 RTV de faixa completa (README dos patches; teste
   `sdkcheck.fixed16-resolve`, 21 verificações). A escala s5.10 é inferida do
   design do título, sem documento público de hardware.

Resultado: build de produção pelo `build-title.ps1` (codegen, reconfigure, build
e recibo renovados), sessão `20261016T114824Z-motocross-fetch-allow`: chão com
terra, sulcos e sombras; zero avisos de fetch; continuam os dois erros de caminho
do conteúdo original. ROV, modo truncado −1…1 e Vulkan não foram alterados nem
testados. Sem medição de desempenho; não há afirmação de ganho ou perda.

### Piloto deformado: causa e correção

O esqueleto entregue por `GetAssets` (71 registros de 96 bytes) tinha **Y/Z
iguais em todos os ossos** (raiz 0,016/−0,067) e rotações identidade: as
translações locais só tinham X. O leitor original dequantiza cada eixo como
`q · base · K[eixo] + offset` (`81970FA0`, parâmetros em `81976D78`), com
`K` em `0x81A975B4`/`0x81A975C0`. Na imagem do `xam.xex` esses vetores valem
**(2, 0, 0)**; os valores reais são calculados pelos construtores estáticos
C++ `81A70E98`/`81A70F50`, que a CRT do XAM roda a partir da tabela em
`0x81A80010`. O serviço AOT nunca executa o entry point do XAM, então Y/Z
ficavam com escala zero. Forçar forma corporal neutra não mudou nada.

Correção (título, sem SDK): o serviço executa, depois de carregar o `xam.xex`
e antes de qualquer algoritmo de avatar, exatamente o bloco de 9 construtores
de constantes do avatar (`0x81A801BC`, `81A70E60`…`81A71008`), verificando cada
entrada da tabela e recusando divergência. São só cálculos de constantes; os
construtores seguintes (objetos com `atexit`) não são executados. Lista em
`titles/common/avatar/xam17559_static_init.h`; teste local
`local.17559 xam.xex avatar static initializer block` (36 asserções contra o
`xam.xex` do usuário). Esqueleto depois da correção: raiz 0,777 m, cabeça
1,191 m, ombros 1,03 m. Build de produção pelo `build-title.ps1`; sessão
`20261016T123045Z-motocross-fetch-allow` (60 s, Xenos, sem teclas enviadas):
piloto inteiro e animado sobre a moto, chão e plateia corretos; log com os dois
erros de caminho do conteúdo original, zero críticas e zero stubs. Continuam
pendentes: roupa/capacete de motocross no avatar (`SetCustomAsset` segue não
implementado), DLC Trick Jam não instalada e save/load.

### Piloto deformado: diagnóstico anterior

O piloto é o par `F7E7173711AA431B/B3FAEF71B6B53FEB` (pulá-lo remove os
estilhaços; a plateia usa outro shader e está correta). Skinning de 4 ossos com
paleta de 72 matrizes 3×4 (`vf94`). Dumps no momento do draw: posições ≤1,2 m,
pesos somam 255, índices <72, 71 matrizes rígidas válidas (det 1) e a 72ª zerada
e sem uso. Forçar o reenvio dos vertex buffers a cada draw não mudou nada.
**Skinning feito na CPU com os mesmos dados já reproduz os estilhaços**: no corpo
(1.137 vértices), 838 arestas passam de 30 cm (máx. 1,14 m), concentradas nos
ossos 33–69 (braços, mãos, dedos); as demais partes não têm nenhuma. Conclusão:
não é tradução da GPU; a pose de ligação da malha do avatar (serviço/XAM 17559)
e a pose animada pelo jogo não combinam nesses ossos (causa na seção anterior).

Terceiro alvo de jogo, a partir de arquivos fornecidos pelo usuário em
`.local/inputs/titles/motocross_madness/` (não versionados; organização em
`metadata/LEIA-ME.txt` daquela pasta). Data: 2026-10-12. SDK: ReXGlue v0.10.0
`f5337cdc` com os patches locais 0001–0033 presentes na árvore no momento do build
(vários escritos por outra sessão durante este trabalho); LLVM 22.1.8; build
`RelWithDebInfo`.

## Entrada

| Item | Valor |
|---|---|
| Pacote | `58410B0F/000D0000/7E38A2BCC89C941C7C6BA32DD79E873F42333EF658`, 1 587 388 416 bytes, SHA-256 `09a3173cfeb4cbd44bf58fc0800cae98aa9aa17584cd036d90d203f3b1d76408`; MD5/SHA-1 iguais aos citados (Redump) na nota do `.7z` recebido |
| Cabeçalho STFS (`x360n-content info`) | magic `LIVE`; content type `0x000D0000`; title id `58410B0F`; media id `62541B78`; versão `0x3`; "Motocross Madness" |
| Extração (`x360n-content extract`) | 55 arquivos, 1 577 889 474 bytes, 0 erros |
| `default.xex` | 19 513 344 bytes, SHA-256 `2a8406bcf9c223f9d7da732be617cc7cd46fbb587ef552f7fcacac2049b95dcd`; único executável |
| DLC | "Trick Jam" (`00000002`) extraída à parte em `dlc/trick_jam/`; **não usada** nas execuções abaixo |

## Etapas

| # | Etapa | Resultado | Evidência / classificação |
|---|---|---|---|
| 1 | Identificação | ok | XEX2 `title`; PE `default.exe`; entry `0x82AA5EA0`; base `0x82000000`; imports `xam.xex` 240, `xboxkrnl.exe` 357; XDK 2.0.21119.0 |
| 2 | Leitura pelo carregador do SDK | ok | imagem de 0x1330000 bytes, SHA-256 `c5bdad1b…ca5e` (dois dumps idênticos); `.text` 0x82210000 + 0xF56A8C; `.pdata` com 37 353 funções |
| 3 | Análise sem ajustes | **falha** | `Validation failed: 10 unresolved calls`; todos os alvos são código sem `.pdata` |
| 3' | Diagnóstico | causa identificada | desmontagem: 3 métodos virtuais folha cujo alvo era o próprio epílogo (`li r3,0; blr`), referenciados só por `.rdata` (vtables sem RTTI); 5 thunks de chamada virtual de 4 instruções; 2 funções alcançadas por adjustor thunks. Entradas em `titles/motocross_madness/analysis/motocross_madness.toml` (início real de cada função) |
| 4 | Compilação (1ª) | **falha** | `motocross_madness_recomp.9.cpp: use of undeclared label 'loc_82C687F8'` (+5); `check-pdata-overlap.ps1`: 1 de 37 353 (`sub_82C680E0`) |
| 4' | Diagnóstico | causa identificada | tabela de saltos por byte sem verificação de limites (`lbzx` em `0x821156B0`, `bctr` em `0x82C68124`), **byte a byte igual** à do Castle Crashers; `[[switch_tables]]` manual de 16 entradas. Depois: 0 sobreposições |
| 5 | Ligação | **falha** | 7 × `undefined symbol __imp__XUsbcam*`: `xboxkrnl_usbcam.cpp` não é compilado no SDK fixado (`src/kernel/CMakeLists.txt:53`) |
| 5' | Correção | ok | `XUsbcamGetState` (0 = desconectada) e `XUsbcamCreate` (`X_STATUS_NOT_IMPLEMENTED`) de `titles/common/camera_state.cpp`, com os chamadores deste XEX conferidos (`src/unresolved_imports.cpp`); os outros 5 são armadilhas `REX_FATAL`. Executável ligado |
| 6 | Execução 1 | **falha** | após ~4 s: `[FATAL] Call to invalid or unregistered function at guest address 0x82ACC350` (método virtual sem `.pdata`, só em `.rdata 0x820E0D24`) |
| 6' | Correção | ok | `scripts/analysis/check-pointer-targets.ps1`: 189 de 2141 endereços passam em todos os critérios, incluindo `0x82ACC350`; 4 conferidos por amostragem; arquivo gerado separado `analysis/motocross_madness_pointer_targets.toml` |
| 7 | Execução 2 | saída voluntária | 18,5 s, exit 0: o próprio jogo chamou `XLaunchNewImage("XLNI_DASH_ARCADE", 0)` (→ `XamLoaderLaunchTitle(NULL, 0x100)`), pela rotina de encerramento `0x831081F8`. O controle do usuário aparece conectado aos 9 s; nenhuma entrada foi enviada por nós. As capturas desta execução foram tiradas cedo demais e não valem |
| 8 | Execução 3 | **tela de título renderizada; bloqueada** | 60 s até o timeout; 0 críticas; exe SHA-256 `5eac2f7a…12fdd`. Captura `.local/runs/mm-captures/run3c-00s.png`: tela de título original ("PRESS A TO START THE GAME", "featuring avatar famestar") com o diálogo do jogo **"MISSING AVATAR PACK — The game can not continue without an installed avatar pack. (A) EXIT GAME"** |

Comando da execução 3:

```powershell
x360n run .local\build\titles\motocross_madness\motocross_madness.exe --label mm-run-3 --timeout 60 --rex-log -- `
  --gpu_plugin=xenos --game_data_root=<raiz>\.local\inputs\titles\motocross_madness\extracted `
  --user_data_root=<raiz>\.local\user\motocross_madness
```

## Bloqueio atual: avatares

`XamAvatarInitialize` do SDK devolve sempre falha (`src/kernel/xam/xam_avatar.cpp:23-33`,
`return ~0u;`, comentário "Negative to fail. Game should immediately call
XamAvatarShutdown"), e os demais `XamAvatar*` importados pelo jogo são `REX_EXPORT_STUB`.
**Inferência, não verificada no código do jogo:** essa falha é o que leva ao diálogo
"Missing Avatar Pack". Seguir exige implementar o subsistema de avatares do XAM com o
pacote de recursos de avatar original do sistema; isso não foi feito.

A etapa de leitura completa (contrato das 10 funções, formato do `.toc`, instrumentação
e avaliação) está em `docs/avatar-system.md`.

### O que o jogo pede (desmontagem de `default.xex`)

`0x82957C40` é a inicialização da biblioteca de avatares compilada no jogo. Ela valida
os argumentos, reserva 0x300000 bytes (`0x82AA67C8`, guardado em `0x8323A0F8`), preenche
uma tabela de 5 callbacks (`0x8295BB28`) e chama
`XamAvatarInitialize(4, r29, r28, &callbacks, buffer, 0x300000)` em `0x82957D34`; um
resultado negativo desfaz tudo (`0x82957BC0`) e volta como erro. Imports de avatar do
jogo: Initialize, Shutdown, GetManifestLocalUser, GetMetadataRandom,
ManifestGetBodyType, SetCustomAsset, GetAssetsResultSize, GetAssets, GenerateMipMaps,
LoadAnimation.

### Como o XAM original faz (XAM 2.0.17559.0, lido como referência, não executado)

Imagem em 0x815F0000 (`x360n-content dump-image`). Tabela em `.data 0x81A97474..80`:
`"\System??\Asset%p"`, `"\System??\AvatarAssetPack"`, `"AvatarAssetPack.toc"`,
`"AvatarAssetPackLegacyV1.toc"`.
- `0x8196B728(caminho)` monta o pacote como `\System??\AvatarAssetPack` (`0x816EFD80`),
  abre `AvatarAssetPack.toc` (`0x8197E238`) e depois `AvatarAssetPackLegacyV1.toc`
  (`0x8197D400`), conferindo a versão de cada um por chamada virtual.
- `0x8196B8C8` escolhe o caminho: primeiro o pacote instalado (`0x816E8D40`, não
  aprofundado); se falhar, `"%s\AvatarAssetPack"` com a raiz `"\System??\GAME:"`, ou
  seja, a cópia que o próprio jogo traz (A World of Keflings traz uma).
- O formato `.toc` não é documentado; o `AvatarAssetPack.toc` da 13604 tem 13 648 884
  bytes (é a base de recursos inteira, não só um índice).

### Componentes originais disponíveis

Pacote de recursos de avatar: PIRS, content type `0x00008000` (`kSUStoragePack`),
title `FFFE07DF`, arquivo `FFFE07DF00000002` de cada atualização de sistema; a versão do
pacote é a do sistema.

| Origem | Versão | SHA-256 |
|---|---|---|
| `SystemUpdate_17559_USB.zip` oficial, extraído em `dashboard/versions/17559/retail_system_assets/storage_packs/` | `0x20449700` (17559) | `9dcef5b4…cd8d6` |
| disco do Forza Horizon 2, `$SystemUpdate` | `0x20409900` (16537) | `1aebbdbd…bd33b` |
| coleção 15574 (= disco do Forza Horizon) | `0x203CD600` | `4c51e93a…` |
| coleção 13604 / 9199 | `0x20352400` / `0x2023EF00` | `950744bc…` / `3a703102…` |
| A World of Keflings, `AvatarAssetPack` | `0x201CB300` (7347) | `beaaae8d…` |

O perfil 17559 deve usar o pacote do zip 17559, sem misturar versões.

### Referência Xenia

Xenia Canary `8d8ba9b6f71e` (2026-09-12), `src/xenia/kernel/xam/xam_avatar.cc`:
`XamAvatarInitialize` devolve sucesso só com a cvar `allow_avatar_initialization`
("Only set true when testing Avatar games. Certain games may crash"); as demais são
`kStub`, algumas com dados fixos. Não há implementação de referência do subsistema.

## Observações

- **`FileAccess.package` é XCTD comprimido** (começa com `0x0FF512ED`).
  `NtQueryInformationFile(XFileXctdCompressionInformation)` (classe 27) não é
  implementado pelo SDK (`X_STATUS_INVALID_PARAMETER`, `xboxkrnl_io_info.cpp:151-160`).
  O jogo tem um caminho alternativo para isso: em `0x82AB2204` ele lê os 4 primeiros
  bytes e compara com `0x0FF512ED`. A tela de título carrega dados desse pacote; se o
  caminho comprimido funciona por inteiro não foi verificado.
- Arquivos que o jogo procura e não existem no pacote:
  `game:\media\GameplayTexts\ENGLISH\ScaleformTranslation.txt` e `dlc_0.txt`
  (`0xC000000F`). Não foram fatais. O `dlc_0.txt` pode ter relação com a DLC, que não
  estava montada; não verificado.
- `x360n-content xref` caía nesta imagem: `.reloc` termina em 0x83341560, depois da imagem
  carregada (0x83330000). Corrigido limitando a varredura à imagem carregada.
- Builds simultâneos de dois títulos colidem: todos gravam a DLL do SDK em
  `external/rexglue-sdk/out/win-amd64` (`CMakeLists.txt:226-228` do SDK). Duas tentativas
  falharam por isso (header do SDK alterado no meio do build; `rexruntimerd.dll` bloqueada).
- XAM 17559 original em AOT no mesmo processo (caminho 2): prova de conceito 21/21 e
  bloqueio atual do `XamAvatarInitialize` original em `docs/avatar-system.md` §8. Sem as
  opções da sonda, o jogo roda como na execução 3.

### Incremento isolado: transformação STRB de cores (2026-10-12)

`20261013T011122Z-mm-avatars-colors-3`, na worktree `x360-avatars`: o XAM
17559 AOT `0x81976598` expandiu 39 tabelas originais em 63 triplas de cores,
com comparação dos 220 bytes de cada saída contra extração independente dos
bits. Três campos truncados foram rejeitados sem escrita; 291 chamadas
preservaram a ABI verificada. Detalhes, dependências e limites em
`docs/avatar-system.md` §10. O contexto do jogo foi restaurado e
`XamAvatarInitialize` continuou retornando `0xFFFFFFFF`; ainda não há avatar
renderizado nem novo marco de jogabilidade. O timeout após a sonda é controlado.
### Incremento isolado: malhas/texturas e callbacks reais (2026-10-12)

`20261013T012506Z-mm-avatars-mesh-2`: o leitor XAM `0x81974A90` montou os
589 assets de campo 3 (634 malhas, 1619 texturas), usando callbacks reais do jogo:
275 chamadas de tiling e 1612 de layout. Validados 1.189.998 índices e 2.871.296
bytes tiled, buffers/ponteiros/ABI e três negativos. Transporte LZX em C++ com a
biblioteca existente do SDK; imports LDI continuam pendentes. Sonda com zero
falhas; jogo encerrado pelo supervisor em 45 s, com os erros XCTD/devkit já
conhecidos. Sem novo marco de renderização ou jogabilidade. A nova opção
`motocross_xam_aot_probe_meshes` pula as suítes anteriores, que não foram repetidas.
Detalhes e limites em `docs/avatar-system.md` seção 11. Somente `x360-avatars`.

### Incremento isolado: LDI e worker GetAssets (2026-10-12)

`20261013T014125Z-mm-avatars-ldi-1`: patch 0035 implementa o perfil LDI com
workspace do chamador e reset por bloco. Stream comprimido original validado em
avanço/cruzamento de 32 KiB/reset/releitura/destruição. Worker `0x81966450` retornou
HRESULT 0 para MaleBody, FemaleBody e Power Tee, com buffers/índices/callbacks/ABI
válidos. Sonda com zero falhas; 46 verificações sintéticas LDI novas passaram.
Initialize permanece em `0xFFFFFFFF`; GetAssets agregado/export e renderização
pendentes. Nenhuma suíte anterior repetida; detalhes e limites na seção 12 de
`docs/avatar-system.md`. Toda a execução ficou na worktree de avatares.

### Incremento isolado: resultado agregado e ciclo de vida (2026-10-12)

`20261013T015915Z-mm-avatars-aggregate-1`: `0x81967628` inicializou o agregado;
`0x81966FC8/0x819669A0` montaram Power Tee + MaleBody pelo worker e stream LDI
originais; `0x819660A0/0x81966348` finalizaram dois descritores e 71 ossos.
Ponteiros/IDs/limites/propriedade/ABI aprovados. Cancelamento e erro AOT real no
segundo asset descartaram a saída privada; liberação/polling terminal conferidos.
Novo coordenador compartilhado C++ com 61 verificações sintéticas aprovadas.
Nenhuma suíte anterior repetida. Build final passou. Export GetAssets/overlapped,
protocolo 2, componentes especiais/modificadores e renderização continuam
pendentes; Initialize mantém `0xFFFFFFFF`. Perfil diagnóstico 4, forma neutra,
apenas na x360-avatars. Evidência, comando e limites em `docs/avatar-system.md` §13.

### Serviço de avatar padrão pela sessão 17559 (2026-10-16)

O serviço de avatar está ligado por padrão e é compartilhado (`titles/common/avatar`).
O jogo recebe o `xam.xex` e o `AvatarAssetPack.toc` somente do perfil da dashboard
Retail 17559 (`preparation.toml [avatar]`), conferidos pelo supervisor antes de cada
início; sem eles, ou a partir de outra versão, a sessão recusa o jogo com mensagem que
identifica versão e arquivo. Registro de biblioteca (`preparation.toml`,
`assets.sha256`) e `game_data_root` adicionados para a My Games. Abertura visual por
esse caminho ainda não verificada. Detalhes em `docs/avatar-system.md` §17.
