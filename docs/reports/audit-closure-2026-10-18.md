# Fechamento da auditoria do x360-native — 18/10/2026

Referência inicial: `main` em `6be0c60`. Trabalho sequencial no mesmo checkout,
sem builds concorrentes, sessões de títulos, Computer Use, push ou Release.
SDK `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, LLVM 22.1.8 e os 91 patches
ativos até 0092 foram preservados. Nenhum recibo de título foi criado à mão;
produtos e recibos vieram exclusivamente de `scripts/build-title.ps1`.

Evidências privadas: `.local/evidence/audit-closure-2026-10-18/` e
`.local/evidence/build-incremental/audit-*`. Os logs/resultados incluem comandos,
prazos, códigos de saída e falhas iniciais. Originais e dados pessoais não fazem
parte deste relatório versionado.

## Tarefas e commits

| Tarefa | Resultado verificável | Commit |
|---|---|---|
| Rebuild incremental: `639e101`, `47d4cd0`, `7151666` e correções posteriores | Remoção de CMake auxiliar reproduzida/corrigida; inventário exato autenticado também no no-op. Headers, SDK/política, flags, interrupção e checkpoints cobertos por regressões focadas. | `ead9ea3` |
| Flags de response files | Reprodução mostrou `@flags.rsp` externo fora da assinatura. Preflight recusa flags externas/aninhadas não expandidas, inclusive pelo ambiente, antes da certificação. Response files declarados pelo Ninja permanecem suportados. | `d8088e6` |
| Charlie: preparação e produtos | Rebuild oficial; 32 módulos compilados, 31 DLLs inspecionadas pelo ABI nativo e todos os conjuntos de endereços comparados aos fontes gerados. Registro e roteamento My Games passaram. | `3c42190` (verificadores) |
| Motocross / sign-in 17559 | Callback `0x82B36410` independente e 51.148 entradas preservadas; `0x90116C88` corrigido na DLL 17559. Cópia de perfil existente passou pelas ABIs XAM e reinicialização. | `fa5730e` e verificadores/documentação |
| Discovery/Castle | Preparação oficial e análise de produção aprovadas; preservados os metadados originais e o switch de 16 slots. Limitação sem o hint reproduzida, sem alegar descoberta genérica completa. | Commit deste relatório |
| SDL, importação e documentação | Foco revisado pela evidência existente; verificação focada de importação e revisão EN/PT-BR/STATUS/checklist/Release descritas abaixo. | Commit deste relatório |

## Rebuild: medições e contratos

Condições: Windows x86-64, PowerShell 5.1 no driver, RelWithDebInfo, oito jobs,
mesmo diretório e Ninja 1.13.1 do Charlie, CMake 3.31.6 e LLVM pinado. Completo
significa reconstruir intermediários nativos no cache existente, sem descarte
dos originais nem medição de instalação/download ou cache frio do sistema.
O harness acrescentou um comentário no host e restaurou bytes e timestamp em
`finally`; a preparação seguinte certificou o fonte restaurado.
Somente as linhas Charlie formam a comparação completo/incremental/no-op.
As demais preparações usam seus diretórios e Ninja originais dos presets;
seus tempos registram a restauração/integridade, não uma comparação entre jogos.

| Charlie / tentativa | Parede | `.obj` compilados | PCH | Resultado |
|---|---:|---:|---:|---|
| Completo `audit-full` | 592,41 s | 962 (958 únicos) | 42 | Aprovado |
| Primeira restauração incremental | 626,03 s | 962 (958 únicos) | 42 | **Sem ganho**: checkpoint recusado por divergência de snapshot; rebuild conservador aprovado |
| Sem alterações | 17,74 s | 0 | 0 | Reutilização verificada |
| Pequena alteração | 388,27 s | 19 | 0 | Incremental aprovado |
| Fonte restaurado | 382,89 s | 19 | 0 | Incremental aprovado |
| Sem alterações, após restauração | 18,03 s | 0 | 0 | Reutilização verificada |
| Preparação final com a proteção de flags `d8088e6` | 610,09 s | 962 (958 únicos) | 42 | Migração de política/rebuild oficial aprovado |
| Sem alterações, política final após os demais títulos | 23,26 s | 0 | 0 | Mesmo recibo reutilizado |
| 17559, com o callback corrigido | 763,52 s | 1.017 (1.013 únicos) | 18 | Build/recibo oficiais e ABI do sign-in aprovados |
| Motocross, callback original preservado | 875,98 s | 1.115 (1.111 únicos) | 12 | Build/recibo oficiais aprovados |
| Castle, metadados originais preservados | 553,71 s | 792 (788 únicos) | 11 | Build/recibo oficiais aprovados |

O seletor invalidou um objeto nos incrementais, mas o Ninja executou também
18 compilações AOT; a tabela informa **19**, não apenas o consumidor selecionado.
O build final levou 15 s em cada incremental. A maior parte da parede restante
foi conferência de dependências/hashes, produtor e publicação. Não há medida de
FPS nem promessa estatística de aceleração. A divergência inicial foi preservada
sem causa presumida; a repetição isolada autenticada manteve o snapshot byte a
byte idêntico (11,35 s), seguida dos dois incrementais e dois no-ops aprovados.
[Metodologia completa e histórico](incremental-builds-2026-10-18.md).

## Charlie Murder: integridade e biblioteca

O build final publicou 4.013 inputs, 35 artefatos e três arquivos do produtor.
Recibo SHA-256 `774ad0418479647d96fda91bdcbdeb869fff4d753b2283617ca1179f03374278`;
EXE `82df123d4b258e0e3bffa81cf7a6bca09c36c0d3e4afc7202e64b239c357f9f7`.

O teste opt-in `tests/local/aot_module_registration.ps1` validou os bytes do
recibo, revisão SDK, inventários, identidade/layout original de cada DLL,
carregamento Windows e ABI de registro. Comparou o SHA-256 de **todos** os
endereços registrados com o fonte autenticado correspondente: 31 DLLs,
27.612 registros no total, incluindo imports. Entry points presentes tiveram
sua função conferida como pertencente à DLL nativa; módulos de recursos sem
entry point não receberam funções fictícias. Uma identidade de entrada incorreta
foi recusada. Nenhuma função guest foi chamada e o recibo permaneceu idêntico.

HostLoader registrou 1.461 funções, incluindo sua entrada `0x82163C60`;
RuntimeHost registrou 19.633. As DLLs nativas têm SHA-256, respectivamente,
`f8b9f280556d6c6e7ff9e7d6e7fb94161da4ac84e586e37a989d41c243ffd317` e
`143522ceae7c9b09361062bc634d63bd12696a502241eca8edb5207037d88825`.

O teste real `[.local-game-registration]` passou 23 asserções em 67,39 s:
publicação pelo código oficial, conteúdo/cabeçalho verificados, destino do pedido
original, rejeição de outra raiz de dashboard, flags incorretas e WindowClose.
Esse ensaio usa a preparação válida do jogo e o catálogo original da 17559;
a dependência nativa da dashboard foi revalidada no fechamento. Não é aceite
de execução XNA, gameplay ou lançamento real pela UI.

O fechamento posterior `[.local-prepared]`, já exigindo **a preparação atual da
dashboard**, passou 22 asserções em 78,73 s. Conferiu os recibos de ambos,
conteúdo/cabeçalho instalado e o pedido original de imagem, rejeitando outra
raiz, flags e WindowClose. Ambos os recibos permaneceram idênticos. A primeira
invocação desse fechamento usou o diretório errado do executável de testes
(nenhum teste iniciou); a repetição usou a saída confirmada em `src/desktop/Debug`.

Após o registro, os nove cabeçalhos anteriores e os 1.065 arquivos monitorados
(entradas de Charlie/Moto/17559, perfis e armazenamento local) mantiveram seus
SHA-256. O novo registro não alterou perfis ou saves.
Na conferência final (7,62 s), os 1.065 arquivos e os nove cabeçalhos iniciais
continuaram idênticos. Há dez cabeçalhos no total, incluindo o registro Charlie.

Conferência final independente dos quatro produtos (`all-prepared-products`,
39,94 s): identidade/inventário dos recibos, todos os inputs/artefatos e os três
arquivos de cada produtor, SDK pinado, globs, configuração CMake exata, cópias
das DLLs comparadas aos outputs Ninja e checkpoint autenticado com banco/política.
Todos aprovados, sem modificar recibos. `prepared-products.json` é diagnóstico,
não um certificado substituto. Inventários finais:

| Produto | Inputs | Artefatos | Inputs CMake | SHA-256 do recibo |
|---|---:|---:|---:|---|
| Charlie | 4.013 | 35 | 203 | `774ad0418479647d96fda91bdcbdeb869fff4d753b2283617ca1179f03374278` |
| 17559 | 4.013 | 13 | 155 | `0b6ca96d328977dbb50901d8569bd1a4559c1a529bcafe75db4b6eff530d6915` |
| Motocross | 4.170 | 6 | 142 | `78d9e7c2f52111d777e5f0d61a6420f4d5b03583dfd0fca21d20757512f8c8dc` |
| Castle | 3.520 | 5 | 139 | `b6b5693e5a4db831e8296fea94df101c713615e5e11286c19680e4f0935761ac` |

## Sign-in 17559 e perfil existente

A falha histórica `LR=90108A40 CTR=90116C88` foi reproduzida por análise, sem
reabrir jogo. O original 17559 tem SHA-256
`86695c6c36250cc4b4addb7ccc07d15d54492b352c979cf5c0934804109a6130`.
O endereço `0x901028C4` nos dados contém o ponteiro para o callback. O corpo
de três instruções termina em `blr`, com SHA-256
`94c836f3f40daf300d3564899db4a92669c797cdcbdc69bbf9750ceb460e9478`.

O dispatcher `0x90116A08` tem limite original de 13 slots, na tabela
`0x901027F8`, relativos a `0x90116A5C` com escala quatro. Todos os destinos
originais ficam antes do callback; o maior é `0x90116C5C`. A análise especulativa
atribuía o callback ao dispatcher: 652 bytes, consulta sem entrada independente.

Os metadados exclusivos da 17559 passam a declarar a fronteira comprovada:
dispatcher de 640 bytes e callback independente de 12. O grafo passou de 1.071
para 1.072 funções, adicionando somente `0x90116C88`, sem remover entradas.
Não foi alterado o scanner compartilhado, o SDK ou os bytes do original.
As 24.441 palavras cobertas permaneceram idênticas. Os fingerprints dos demais
corpos C++ existentes também permaneceram idênticos; somente o dispatcher foi
alterado, além do novo corpo independente. Três destinos especulativos saíram
da tabela analisada, preservando os 13 destinos efetivos do original.

A preparação 17559 publicou 4.013 inputs, 13 artefatos e três arquivos do
produtor; recibo `0b6ca96d328977dbb50901d8569bd1a4559c1a529bcafe75db4b6eff530d6915`.
O teste completo do sign-in passou em 2,90 s, incluindo a DLL final com 1.072
registros e o callback pertencente ao próprio módulo. SHA-256 da DLL:
`46050361b6f4f8c82ddd718c0c5598216426e26cf187ca4f990f0d2353221fbc`.

O teste `[.existing-profile-copy]` passou **29 asserções**: registros reais
copiados, seleção da identidade existente, slot zero, `XamUserLogon`, ABI
estendida `XamUserLogonEx`, consultas XAM, rejeição de máscara XUID inválida e
persistência após reinicializar o runtime. O teste aceita escrita somente numa
cópia canônica sob `.local/evidence`; não chama `CreateLocal` e conserva o número
de contas. Não foram carregados XEX nem interfaces originais nesse teste.

O callback Motocross `0x82B36410` tem corpo independente, sem stub nem caso
englobado pelo dispatcher `0x82B36390`. O novo produto conserva exatamente as
51.148 entradas do registro anterior, sem perdas ou adições; os fontes foram
autenticados pelo recibo real. Verificação em 12,73 s; recibo SHA-256
`78d9e7c2f52111d777e5f0d61a6420f4d5b03583dfd0fca21d20757512f8c8dc`.
Dois testes com os perfis reais do repositório passaram 13 asserções em 37,33 s:
recibos de ambos os produtos, arquivos/argumentos de avatar da 17559 e recusa de
outra dashboard. Nenhum original foi iniciado.

## Discovery/Castle, foco e referências públicas

O Castle original continua com SHA-256
`825e9c8cb951d62ce954901488c5b783349c98cc5c03bd08ea0739df3af5d9ad`.
A análise com **o arquivo de produção atual** passou: 3.910 funções, 252.627
palavras cobertas e zero erros. Os adjustor/leaf `0x820E15E0` e `0x820E15E8`,
`0x82114E20` e o destino adjacente `0x82112D10` são entradas independentes;
as entradas necessárias constam no registro gerado. O switch autoritativo de
`0x82112C04` mantém 16 slots. O ensaio levou 16,77 s.

Retirar **somente** o hint `0x820E15E8` numa configuração diagnóstica isolada
reproduz `UnresolvedCall 0x820E15E8 from 0x820E0D04`, exit 1. É a limitação
documentada de descoberta sem metadados, não um resultado aprovado nem uma
regressão do perfil suportado. Os hints comprovados e o switch original foram
preservados; não foi acrescentada uma heurística global para promover desvios
arbitrários. A descoberta completamente automática desse caso continua limitada.
[Análise anterior e procedência](castle-tail-boundary-2026-10-17.md).

Foco SDL: revisão de `src/supervisor/session_window.cpp` e do
[relatório de 18/10](session-focus-2026-10-18.md). A reconciliação é assíncrona,
valida a relação pai/filho e só foca a filha se a raiz for foreground e não
minimizada. O código não mudou nesta auditoria. Reutilizada a evidência anterior
de seis ciclos, duas filhas e 89 asserções, sem contá-la como execução nova.
Não foi repetido o teste que altera o foco do desktop.

Importação: seleção `[desktop][extraction],[generic-aot]` do executável desktop
atual passou sete casos/175 asserções em 3,04 s. Cobriu ISO/GDFX, CON/LIVE/PIRS
sem extensão, interrupção/falha sem publicação, inventário incompleto, fontes
diretas e preservação do original/save; os casos AOT cobriram projeto estável,
identidade e dependências verificadas por conteúdo. Fixtures sintéticos não
substituem a validação dos produtos reais descrita acima. Não foi reexecutada
a suíte completa do desktop nem a GUI de importação.

README EN/PT-BR, matriz, STATUS e checklist foram alinhados: bloqueios históricos
do Charlie e de terreno/piloto não são apresentados como estado técnico atual;
os aceites de gameplay e saves continuam abertos. A seção duplicada de builds
incrementais foi consolidada, com os limites de response files documentados.

Consulta de leitura com GitHub CLI/API confirmou a prévia pública mais recente:
[`v0.1.0-preview.21`](https://github.com/fabegalo/x360-native/releases/tag/v0.1.0-preview.21),
publicada em `2026-10-16T05:50:07Z`, não draft, com ZIP e SHA-256. Tag na revisão
`87b86ec7b7b46aa956d33043d8379ec04c9a50c8`; corrigidos os links atuais à `.20`.
Notas versionadas e referência histórica à primeira `.19` foram preservadas.
Metadados do asset ZIP informam 5.249.707 bytes e digest
`fa29d435b0ba2d5075def32259e57444911e7563ae7fc801249f28a942891766`;
isso **não é** download/revalidação do pacote ou aceite em Windows limpo.
O acesso web inicial não retornou a página; a verificação efetiva veio da API
autenticada pelo CLI. Logs JSON privados: `release-list.json` e
`release-preview21.json`. Nenhuma publicação foi feita.

## Testes efetivamente executados

| Teste | Resultado final | Tempo |
|---|---|---:|
| Incremental ampliado, incluindo CMake removido e flags externas | Aprovado | 75,66 s |
| Toolchain: inventários/ambiente e recusa de flags externas sem substituir snapshot | Aprovado | 48,04 s |
| Publicação atômica de recibos | Aprovado | 2,37 s |
| Travas de build | Aprovado | 1,92 s |
| Trava do AOT genérico / invalidação por build direto | Aprovado | 5,78 s |
| Limpeza seletiva nativa | Aprovado | 2,81 s |
| 31 DLLs reais e conjunto completo de endereços | Aprovado | 16,35 s |
| My Games real, sem executar títulos | 1 caso / 23 asserções aprovadas | 67,39 s |
| My Games com jogo e dashboard atualmente preparados | 1 caso / 22 asserções aprovadas | 78,73 s |
| Fronteira estática corrigida do sign-in | Aprovado; +1 entrada, nenhuma removida | 0,69 s |
| Original/sign-in de produção: fronteira, corpo e registro nativo | Aprovado | 2,90 s |
| Perfil existente em cópia, duas ABIs e restart | 1 caso / 29 asserções aprovadas | 0,38 s |
| Motocross: callback, registro e recibo reais | Aprovado; 51.148 entradas preservadas | 12,73 s |
| Dependência Motocross/17559 e recusa de outra versão | 2 casos / 13 asserções aprovadas | 37,33 s |
| Castle: produção, tabela/entradas e negativo sem hint | Produção aprovada; negativo esperado reproduzido | 16,77 s |
| Quatro preparações atuais: bytes, produtores, CMake, DLLs e checkpoints | Todas aprovadas | 39,94 s |
| Extração/importação e AOT genérico focados | 7 casos / 175 asserções aprovadas | 3,04 s |
| Pin e 91 patches SDK: `apply-sdk-patches.ps1 -Check` | Aprovado, sem alteração | 10,28 s |
| SHA-256 de inputs/perfis/saves e cabeçalhos anteriores | 1.065 arquivos + 9 cabeçalhos preservados | 7,62 s |
| Destinos relativos/imagens em 11 documentos revisados | 205 destinos aprovados; URLs externas/âncoras excluídas | 1,84 s |

Não foi executada suíte completa. Resultados históricos de gameplay/SDL/PPC
permanecem identificados pelos seus relatórios, sem contá-los como novos testes.

Falhas iniciais preservadas: CMake auxiliar removido aceito; RSP externo sem
invalidação; tentativas de testes presas no sandbox; seleções CTest com zero casos;
captura parcial de stderr no negativo da toolchain; tipo opcional do entry point
no primeiro build do probe; raiz/caminho de cmd incorretos no coletor; máscara
XUID zero usada inicialmente como positiva no novo fixture; callback englobado
no grafo anterior; comparador privado quadrático de 51 mil entradas interrompido
e substituído por conjuntos (51,87 s da tentativa abandonada). Os fixtures foram
corrigidos e os testes afetados repetidos. O primeiro verificador final de
preservação encapsulou o array JSON numa segunda camada no PowerShell 5.1;
diagnóstico confirmou `wrapped=1/direct=1065`, paths reais de até 116 caracteres.
Corrigida somente a leitura do harness, sem mudar a biblioteca de hashes.
Nenhum resultado parcial ou binário de build falho foi tratado como aprovação.

## Limites de aceite

Execução real de Charlie/XNA, Motocross com perfil existente pelo My Games,
sign-in/Guide/retorno visual e gameplay/save/load comercial dependem de novo aceite
manual autorizado. Windows limpo, obtenção guiada de ferramentas e primeira
utilização integral também continuam pendentes. Compilação, registro nativo e
contratos de arquivos/ABI não certificam esses fluxos.
