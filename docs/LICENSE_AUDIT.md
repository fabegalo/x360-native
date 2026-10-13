# C10 — licenças e distribuição

Auditoria de 13/10/2026. **Implementação técnica do C10 concluída; primeira
pre-release pública expressamente autorizada pelo mantenedor.**
Licença BSD-3-Clause, inventário, avisos/textos, procedência do novo ícone,
verificação de conteúdo e empacotamento reproduzível estão implementados e
validados. Isso não encerra as permissões de redistribuição. A preparação da
primeira Release é registrada em [FIRST_RELEASE.md](FIRST_RELEASE.md).
Os registros abaixo descrevem a auditoria e os candidatos anteriores à Release.
As referências históricas a bloqueios não são portões ativos para a publicação
autorizada de `v0.1.0-preview.19`. Obrigações Microsoft, GPL/LGPL e direitos sobre
originais continuam aplicáveis; a autorização não equivale a parecer jurídico,
prova de titularidade de terceiros ou licença para redistribuir o SDK completo.
O mantenedor aprovou BSD-3-Clause e o titular `x360-native`; `LICENSE` contém os
termos definitivos somente do código próprio. Comparação em `LICENSE_PROPOSAL.md`.

## Escopo verificável

O inventário detalhado está em [licenses/inventory.json](../licenses/inventory.json):
origem, revisão, SPDX quando estabelecido, local original, SHA-256 dos textos,
forma de integração, presença no desktop ou download separado e obrigações.
Inclui as bibliotecas transitivas documentadas nos arquivos de MinGit/CMake.
`NOASSERTION` significa que a expressão completa não foi estabelecida; não é uma
licença permissiva implícita. As versões transitivas não identificadas são
referenciadas pela identidade exata do arquivo upstream que as contém.

Base SDK: `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2` (v0.10.0), com os 38 patches
já fixados em `deps.lock.json`. LLVM 22.1.8, MSVC 14.44.35207 e SDK Windows
10.0.26100.0. Não foram alterados SDK, patches, contratos PPC, runtime ou AOT.

| Material efetivamente distribuído no desktop | Licença / integração / obrigação |
|---|---|
| ReXGlue/Xenia | BSD-3-Clause, 20 unidades dos leitores ISO/STFS e utilitários no extrator, headers; conservar copyright, termos, disclaimer e não endosso. Atribuições originais por arquivo em `licenses/rexglue-attributions.txt`. |
| fmt | MIT, estática; texto e copyright. |
| CLI11 | **BSD-3-Clause**, headers; não MIT. Texto e copyright. |
| toml++, spdlog | MIT; toml headers, spdlog estática com fmt externo. Texto e copyright, incluindo contribuições transitivas listadas em `header-attributions.txt`. |
| utfcpp | **BSL-1.0**, headers; texto preservado, inclusive exceção de código objeto. |
| nlohmann/json 3.11.3 | MIT, header vendorizado por inja; faltava no ZIP anterior. Texto original da árvore fixada (2013–2021) e atribuição do header (2013–2023 Niels Lohmann) conservados separadamente. |
| Microsoft STL | Apache-2.0 WITH LLVM-exception, templates/objetos; texto original VS 17.14 incluído. A exceção trata partes incorporadas em objetos, não licencia o CRT proprietário. |
| Microsoft CRT/UCRT | Estático Release `/MT`, sujeito aos termos de código distribuível da instalação Visual Studio/SDK. Confirmar habilitação do distribuidor. DLLs Windows apenas importadas, não incluídas. |
| SDL | Zlib; só trechos nos patches, com modificações identificadas. SDL binário não entra no desktop. |
| Código próprio/patches locais | BSD-3-Clause aprovada; avisos de arquivos de terceiros continuam aplicáveis. Adicionada atribuição explícita ao `xex_patch.cpp` adaptado do SDK/Xenia. Nenhuma semântica alterada. |

Inja não é usado como engine no desktop: somente seu nlohmann/json vendorizado.
SIMDe está no include path, mas nenhum header seu apareceu nos 481 inputs SDK
reais do desktop; seu texto é incluído preventivamente, não como prova de vínculo.
O header JSON inclui avisos Hedley, Grisu2, UTF-8 e Abseil; foram preservados.
O trecho Apache Abseil só é habilitado sem C++14, portanto não é compilado em C++23.
Catch2 é BSL-1.0 e só entra nos executáveis de testes, que não são distribuídos.
Os textos destes componentes também são conservados para transparência.

## SDK completo e ferramentas obtidos separadamente

Não estão nos três executáveis do ZIP. `prepare-tools.ps1` obtém arquivos
upstream completos, preserva avisos e verifica os hashes já existentes. SDK e
submódulos são obtidos das revisões fixadas; a recursão inclui dependências
opcionais mesmo quando não são compiladas. Nenhum espelho próprio foi criado.

| Componente | Uso real / termos / consequência se redistribuído |
|---|---|
| SDL3 | Estático nos títulos/runtime; Zlib. Conservar aviso, identificar modificações. |
| FFmpeg | `libavcodec`/`libavutil` estáticas no runtime. LGPL-2.1-or-later: configuração observada GPL=0, NONFREE=0, VERSION3=0. 161 arestas de compilação no Ninja Debug, sem cabeçalho GPL encontrado nos arquivos selecionados. Não se concluiu só pelo nome FFmpeg. Distribuição do runtime exige fontes/avisos e via de modificação/relinkedição conforme LGPL. |
| libmspack | Compila `cabextract/mspack/lzxd.c`, cujo cabeçalho declara **LGPL-2.1-only**; não classificado pela licença do programa cabextract. Estático no runtime; mesmas questões de relinkedição. |
| decoder disasm | `thirdparty/disasm/ppc-dis.c`: **GPL-2.0-or-later**, com copyright FSF. O grafo Ninja vincula `disasm` em `rexglue` via rexcodegen e diretamente em `x360n-content`. Distribuir esses executáveis não pode ser justificado apenas pela BSD do SDK; exige cumprir GPL e disponibilizar código-fonte correspondente da combinação. |
| PowerPC binutils | GPLv3 no texto fornecido; programas independentes usados pelos testes PPC. Invocação externa não impõe automaticamente GPL à GUI. Proveniência/versão dos binários, fonte correspondente e DLLs Cygwin/iconv/intl precisam ser confirmadas antes de espelhá-los. O executável `as` falhou por realocação da DLL neste ambiente durante a auditoria. |
| LLVM/Clang 22.1.8 | Ferramenta independente, Apache-2.0 WITH LLVM-exception e avisos adicionais do texto original. Uso do compilador não torna o aplicativo GPL. Arquivo oficial fixado no lock, não no ZIP desktop. |
| CMake 3.31.6 | Ferramenta BSD-3-Clause, com curl, libarchive, lzma, libuv, rhash, nghttp2, KWSys, zlib, zstd e cppdap. Licenças transitivas preservadas no arquivo upstream; caminhos/hashes inventariados. |
| Ninja 1.13.1 | Ferramenta Apache-2.0; incluído texto original do tag, pois o ZIP da ferramenta não basta como aviso no nosso pacote. |
| MinGit 2.56.0.2 | Git GPL-2.0-only, programa independente. Inclui componentes MIT/BSD/Apache/Zlib e GPL/LGPL com exceções, não todos sob GPL de Git. Inventário lista os diretórios legais e hashes; expressão completa de alguns pacotes mistos permanece pendente para eventual redistribuição. |
| Visual Studio Build Tools | Instalador separado, assinado Microsoft e sujeito a consentimento/termos próprios. O endpoint oficial é móvel, não representa versão fixada redistribuída por nós. |

Outros componentes identificados no SDK: imgui MIT, o1heap MIT, xxHash BSD-2-Clause
(CLI xxhsum não utilizado), Tracy BSD-3-Clause no Debug; aes_128 e DES MIT;
TinySHA1 ISC; RC4 com termos Whistle; Rijndael com declaração de domínio público;
stb MIT ou Unlicense; RenderDoc header MIT. Há lacunas nos termos vendorizados de
tiny-AES-c, disruptorplus, SHA256 de Stephan Brumme (declaração Zlib), PE Microsoft,
headers DXC (declaração NCSA) e wrapper AMD do checksum DXBC/RSA-MD5. O inventário
registra-as; não foram reclassificadas como BSD. DXC declara origem
`microsoft/DirectXShaderCompiler@6b6f40200bea5ed99367513f53f6a28e52fd3d0e` em seu README.
Vulkan, glslang, SPIR-V, VMA e MoltenVK estão inventariados como opcionais
baixados, desabilitados no build D3D12 Windows; suas licenças mistas não afetam
os três executáveis desktop. FidelityFX está desabilitado e não é obtido por este fluxo.

Evidência de vínculo: `src/content/CMakeLists.txt`, CMake do SDK e arestas reais
`LINK_LIBRARIES` em `.local/build/c5-main/CMakeFiles/impl-Debug.ninja`. O runtime
incorpora os objetos de áudio/LGPL; não foi identificado vínculo de `disasm` no
runtime. Não se atribui GPL aos títulos apenas por executar um gerador separado.

## Conteúdo e histórico

O empacotamento exporta somente fontes comprometidas e um SDK privado fixado para
compilar; o SDK completo e seu `.git` não são exportados ao payload. Os arquivos
originais dos usuários permanecem em `.local`, ignorada. Saída AOT é ignorada;
os arquivos `titles/*/generated/rexglue.cmake` são configuração, não código de jogo.
Wrappers/imports manuais dos títulos não foram confundidos com saída AOT.

A varredura de 760 blobs alcançáveis em todas as refs locais (8.506.806 bytes,
antes deste trabalho) não encontrou magic de XEX/STFS, a constante não nula do
loader verificada ou marcadores de chave privada. Busca de extensões no histórico
não identificou jogos, módulos, fontes Xbox, dumps, binários ou AOT adicionados.
Não examinou objetos inalcançáveis nem histórico dos submódulos. Não é uma
garantia geral de ausência de segredos ou de direitos sobre todo material textual.
Não houve remoção nem reescrita do histórico. Log: `.local/audit/c10-history.log`.

O extrator desktop não compila `src/system/xex_module.cpp`: as constantes de
decriptação desse loader permanecem no SDK obtido separadamente, não no extrator
nem no payload. O gate procura a constante não nula em binários e texto sem
imprimir seu valor; zeros de devkit são indistinguíveis de preenchimento comum.
`xex_patch.cpp` lê chaves locais fornecidas pelo usuário e não as exporta.
Os testes usam dados sintéticos, não arquivos Xbox originais.

## Correção e validação do empacotamento

`THIRD_PARTY_NOTICES.md`, `LICENSE` e `licenses/` ficam na raiz do ZIP, com indicação
no READ-ME. Textos de dependências são copiados byte a byte das revisões fixadas,
ou do tag oficial indicado. `.gitattributes` conserva esses bytes, mantendo a
regra prévia de patches. O gate compara os textos disponíveis no SDK da build.

`check-package-licenses.ps1` verifica inventário, revisão dos componentes, licenças,
atribuições, lista dos três executáveis, payload, compilação e headers transitivos
reais (`ninja -t deps`). Rejeita arquivos extras, extensões proibidas, XEX/STFS
renomeados, executáveis desconhecidos, saída generated e constantes sensíveis.
Usa os índices C4/C11; não cria outro fingerprint de preparação. O checker é um
gate de produção do candidato local, não uma certificação jurídica nem uma
assinatura contra adulteração de todo o pacote por terceiros.

Testes focados: `tests/packaging/licenses.ps1`, com um candidato real e negativos
mutados em diretórios isolados, incluindo índices recalculados para provar que
um hash autoconsistente não autoriza conteúdo indevido. Resultados do pacote e
testes são registrados na seção de aceitação ao final deste relatório.

## Pendências na primeira aceitação (histórico; atualização ao final)

1. Confirmar origem/licença do ícone `src/desktop/desktop.ico`. É um recurso
   incorporado; o titular aprovado para o projeto não confirmou sua autoria.
2. Confirmar a habilitação e os termos de redistribuição do CRT/SDK da instalação
   Microsoft usada. O texto Apache da STL não cobre essa questão.
3. Resolver o tratamento público do SDK completo: faltas de proveniência/legal
   indicadas acima, avisos/fontes dos binutils e DLLs; registrar como serão
   atendidas as obrigações GPL/LGPL **antes de distribuir esses binários**.
   O ZIP desktop atual não os inclui. Baixar upstream para uso local e espelhar
   binários em Releases são cenários distintos; esta auditoria não aprova o segundo.
4. C4/C11 continuam com validação em Windows limpo pendente. Nenhuma nova alegação
   de aceite gráfico ou compatibilidade de jogos foi feita nesta tarefa.

Não falta mais a decisão de licença própria. Estes bloqueios são específicos,
não uma alegação de que toda dependência GPL impede um projeto permissivo.

Referências primárias: [FFmpeg legal](https://ffmpeg.org/legal.html),
[política LLVM](https://llvm.org/docs/DeveloperPolicy.html#license),
[redistribuição Visual C++](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170),
[BSD-3-Clause](https://spdx.org/licenses/BSD-3-Clause.html),
[STL VS 17.14](https://raw.githubusercontent.com/microsoft/STL/vs-2022-17.14/LICENSE.txt).
Os termos integrais copiados, e não esta síntese, são as condições das licenças.

## Aceitação do candidato local

Revisão de código do pacote: `3b9cb59228d9a73277c924ae5012377f43706488`.
O commit posterior desta seção é somente documentação dos resultados, sem
substituir a identidade registrada no ZIP. Foram geradas duas builds novas,
simultâneas, com oito workers cada, nos diretórios isolados
`.local/distribution/81dca564065f4be29439e76b00b1f43b/build` e
`.local/distribution/1217a50d613a4e13af405ddf23908587/build`.

- ZIP: `.local/packages/c10-validated/x360-native-0.1.0-preview.6-windows-x64.zip`.
- Repetição: `.local/packages/c10-validated-repeat/x360-native-0.1.0-preview.6-windows-x64.zip`.
- Ambos têm **3.086.785 bytes**, **378 arquivos**, e SHA-256 idêntico:
  `80f43bb1da2fd74ef8eb8efd2789c45268a1d38a4eb4835730f44dde3d34d8fb`.
- Identidade do payload:
  `9bcb8699b29c78c040522662b7a4cc018ce18518e81d5efdbb6faf2d02d0ed0c`.
- Inventário final: **88 registros**, incluindo recursos com pendência explícita;
  **102 blocos originais de atribuição SDK**, textos de terceiros e copyrights
  dos headers. Não implica que os 88 componentes estejam vinculados à GUI.
- Escopo real: **20 unidades SDK**, **481 inputs SDK** descobertos pelo compilador,
  **4 bibliotecas estáticas próprias/de terceiros** no grafo de vínculo, além
  do runtime estático Microsoft; **3 EXEs**, sem DLLs adicionais no ZIP.

| Verificação executada | Resultado |
|---|---|
| CTest de cada build final | 55 casos, 20.628 assertions aprovadas em cada build. |
| Gate original/licenças/conteúdo | Aprovado nas duas builds; índices e avisos completos. |
| ZIP extraído fora do repositório | Aprovado em `C:/Users/fabeg/AppData/Local/Temp/x360-c10-acceptance/x360-native-0.1.0-preview.6-windows-x64`. |
| Testes PowerShell 5.1 sobre o pacote extraído | **1 positivo + 16 negativos aprovados**. |
| Inicialização dos componentes de console com PATH apenas Windows | Supervisor `--help` retorna 0; extrator imprime uso e retorna 3, conforme seu contrato. Não foi um novo teste gráfico. |
| Recibos C2 reais de Castle Crashers, dashboard 17559 e Iruka | **8 assertions aprovadas**, usando a nova suíte desktop contra `.local/build/c5-titles`. Nenhuma preparação recompilada/invalidação artificial. |
| Isolamento C5 e reprodução C4/C11 | Duas builds simultâneas isoladas, ZIPs idênticos. |
| Contratos anteriores | SDK/LLVM, 38 patches, CMake PPC/C6, leases/C5 e código dos recibos/C2 sem alterações. Patch 0038 conserva SHA-256 `b4c6a4fd27ee6ec4207cc2180aa7ca98b5b36f560484d608c59ea72b03e9d6b6`. |

Os negativos cobrem licença ausente/alterada, atribuição SDK ausente, DLL extra,
EXE extra/ausente/substituído, XEX sintético renomeado, fonte proibida, arquivo
generated, novo header, biblioteca estática desconhecida, nova unidade de
compilação, revisão divergente, componente sem identificação e constante do
loader. Os índices são recalculados nos testes de conteúdo: a rejeição não
depende apenas de hashes antigos. Nenhum dado real de usuário foi modificado.

Logs: `.local/audit/c10-package-validated.log`,
`c10-package-validated-repeat.log`, `c10-license-tests-final.log`,
`c10-receipts.log`, `c10-result.json` e `c10-preserved-contracts.json`.
Os candidatos intermediários que falharam no gate não produziram ZIP.

Limite da ferramenta de auditoria: o Windows PowerShell 5.1 encontrou seu limite
legado de caminho ao ler um arquivo sob o diretório temporário longo do sandbox.
A extração e a bateria final foram repetidas com sucesso no caminho curto acima.
Isso não comprova o comportamento da GUI em caminhos longos; o gate de produção
foi validado nos caminhos isolados registrados. Windows limpo não estava
disponível. Não foram repetidas regressões gráficas de jogos nesta tarefa de
licenciamento; as mudanças nativas se limitam a comentários de atribuição.

Inspeção adicional dos binutils encontrou a string de versão **GNU Binutils 2.24**
no `as.exe`; como sua execução falhou e faltam dados da montagem/distribuição,
isso não resolve a proveniência ou o fornecimento de fonte correspondente.

## Fechamento das pendências verificáveis — 13/10/2026

Esta atualização reaproveita os **88 registros**, não refaz a auditoria geral.
O mantenedor confirmou **x360-native** como identificação pública do titular das
contribuições próprias. `LICENSE` continua BSD-3-Clause, sem atribuir a ele direitos
de Microsoft, Xenia ou outros autores. A identificação civil e a titularidade de
eventuais contribuições futuras não são inferidas desse nome público.

### Ícone e avisos recuperados

O ícone sem criação documentada foi substituído por geometria XN original,
preservando monograma, cores e recurso Windows. O gerador não usa fontes ou
imagens externas. [Procedência e reprodução](ICON_PROVENANCE.md) documentam também
o blob antigo: ele permanece no histórico, fora do novo ZIP e de seu payload.
Publicar esse histórico antigo requer decisão específica; não houve reescrita.
O gate valida o ICO contra o inventário e contra a geometria reproduzível.

| Pendência existente | Evidência obtida e correção |
|---|---|
| tiny-AES-c | `aes.c`, `aes.h`, `aes.hpp` byte a byte iguais a kokke/tiny-AES-c `23856752fbd139da0b8ca6e471a13d5bcc99a08d`. [Unlicense original](https://github.com/kokke/tiny-AES-c/blob/23856752fbd139da0b8ca6e471a13d5bcc99a08d/unlicense.txt) incluída. |
| disruptorplus | Referência xenia-project/disruptorplus `5787e9cb7551c8c79cf9ce14f7be860dc907e9a4`: dez headers idênticos; em `multi_threaded_claim_strategy.hpp`, o cast de `diff + 1` passa de `sequence_t` para `size_t`. [MIT de Lewis Baker](https://github.com/xenia-project/disruptorplus/blob/5787e9cb7551c8c79cf9ce14f7be860dc907e9a4/LICENSE.txt) recuperada. |
| DXC headers | Os dois headers são idênticos ao commit `6b6f40200bea5ed99367513f53f6a28e52fd3d0e` já documentado pelo SDK. [LICENSE.TXT integral](https://github.com/microsoft/DirectXShaderCompiler/blob/6b6f40200bea5ed99367513f53f6a28e52fd3d0e/LICENSE.TXT) incluída, com NCSA e demais avisos. |
| DXBC checksum | Origem [AMD ShaderUtils](https://github.com/GPUOpen-Archive/common-src-ShaderUtils/tree/545195d74ff0758f74ebf9a8b4c541f9390a1df7/DX10) confirmada por diff dos dois arquivos. Adaptações Xenia removem Windows.h, usam tipos C++ e retorno void. MIT AMD recuperada junto dos avisos RSA originais; a documentação identifica a derivação do RSA Data Security, Inc. MD5 Message Digest Algorithm. |
| SHA256 | Conservada a declaração do SDK de aquisição sob zlib e alteração para Xenia. Incluídos os [termos do próprio autor](https://create.stephan-brumme.com/disclaimer.html), semelhantes a zlib, sem rotulá-los como texto SPDX Zlib idêntico. A data/texto histórico da aquisição continua sem prova independente. |
| GNU decoder e notices inline | GPL v2 integral adicionada para a opção GPL-2.0-or-later declarada no decoder. TinySHA1, RC4, Rijndael e RenderDoc agora têm seus comentários legais completos também em `licenses`, além dos fontes baixados. |

As revisões acima são **referências de proveniência**, não atualizações do SDK.
O inventário mantém a revisão ReXGlue original, registra URLs/hashes dos arquivos
comparados e os textos entregues. A revisão `f5337cdc...`, os 38 patches e o
compilador não foram alterados. Os suplementos acompanham a raiz do ZIP e o
payload, portanto a pasta `licenses` dos ambientes criados/atualizados. Nada foi
injetado no checkout SDK e não foi criado outro fingerprint de preparação.

### Microsoft: alcance concreto dos termos consultados

| Material | Presença e condição |
|---|---|
| Jogos ISO/STFS, dashboard, fontes, perfis e saves | Fornecidos pelo usuário, fora do ZIP. Posse não comprova autorização para qualquer extração, transformação ou redistribuição. Os contratos variam por produto/região. |
| C++/binários AOT derivados desses arquivos | Gerados localmente, fora do ZIP. A geração local não transforma o código do jogo em contribuição própria BSD. A habilitação para usar ferramentas e os direitos sobre o conteúdo são perguntas separadas. |
| Três EXEs desktop | Código próprio e componentes listados no inventário, incluindo STL Apache com exceção LLVM e CRT/UCRT **estático Release proprietário**. Não há redistribuível instalável ou DLL Microsoft copiada no ZIP. |
| Build Tools e Windows SDK | Obtidos/instalados separadamente. São usados na compilação; seus executáveis, headers e bibliotecas não são exportados como arquivos do pacote. Seus objetos CRT/UCRT incorporados continuam sujeitos aos termos pertinentes. |
| ReXGlue completo | Baixado upstream na revisão fixada; não exportado no ZIP. Headers DXC com NCSA não têm o mesmo regime de software proprietário Xbox. O loader do SDK contém constantes cuja análise jurídica não foi resolvida; o gate exclui essas constantes do pacote desktop. |

Fontes oficiais conferidas em 13/10/2026:

- [Build Tools 2022, termos de março/2024](https://visualstudio.microsoft.com/wp-content/uploads/2024/03/Visual-Studio-2022-Diagnostic-Build-Tools-Agent-License_Update-March-2024_EN.docx),
  EULAID `VS_2022_Tools_2024Mar_ENU.1033`, seções 1 e 2. Exigem licença Visual Studio
  válida como regra; a exceção sem essa licença cobre dependências C++ de terceiros
  sob licença OSI e limita suas modificações. **Não demonstram autorização geral
  para desenvolvimento próprio ou compilação AOT proprietária.** Clang não elimina
  os termos dos componentes Microsoft usados no vínculo. Confirmar a habilitação
  do responsável e o enquadramento do fluxo local antes da publicação.
- [Visual Studio Community 2022](https://visualstudio.microsoft.com/wp-content/uploads/2021/11/Visual-Studio-2022-Community-License-EN.docx),
  seções 1, 3 e 4, e [lista oficial de redistribuição](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution).
  A lista depende da licença aplicável; debug_nonredist não pode entrar na
  distribuição. O texto requer proteção contratual do código redistribuível aos
  usuários e distribuidores. Os avisos BSD/Apache existentes não constituem,
  sozinhos, esse acordo. Não foi criada uma EULA por suposição nem aceitos termos
  em nome do mantenedor. A lista de DLLs não foi usada como prova automática para
  toda biblioteca estática.
- Contrato instalado **Windows SDK 10.0.26100.0**, em
  `Windows Kits/10/Licenses/10.0.26100.0/sdk_license.rtf`, seções 1 e 2(a): contém
  condições para código redistribuível, incluindo distribuir `.lib` apenas
  incorporada por linker, funcionalidade principal adicional, avisos e proteção
  contratual. O link REDIST `https://go.microsoft.com/fwlink/?LinkId=524842`
  não retornou conteúdo verificável nesta consulta. Portanto a correspondência
  completa da lista aplicável com CRT/UCRT e a habilitação não são dadas por
  encerradas. [Documentação oficial UCRT](https://learn.microsoft.com/en-us/cpp/porting/upgrade-your-code-to-the-universal-crt?view=msvc-170)
  distingue a UCRT do SDK e o vcruntime do Visual Studio.
- [Manual/licença oficial Xbox 360 S](https://download.microsoft.com/download/4/A/6/4A631340-39F3-401F-8DE4-FF82C5879F6E/X175377601mnl-xbox-360s-console-manual-en-da-nl-fi-fr-de-it-no-pt-es-sb-we.pdf),
  seção Software License: restringe o software do console e prevê limitações de
  cópia, engenharia reversa e contorno de proteção. É evidência concreta para
  revisão do uso de dashboards; não substitui o contrato específico de cada jogo,
  console/região nem determina o efeito de exceções legais locais. Não se aplicou
  por engano o contrato do aplicativo Xbox para PC a todo software Xbox 360.

Evidência local dos textos consultados em `.local/audit/c10-provenance`:
Build Tools DOCX SHA-256 `2f66b86a00e8d9833789897ce23d05a4a2dbea370cf39c8c1098dbc17d0e7bdc`;
Community DOCX `41a207b10c8ab91d0d2f10a854715f73dca54509581692d2fe179aa3ffcb8540`;
SDK RTF instalado `0f4a26ac9dc50066f8a1bfeaaf3f092d1b9e4791df5487b9f3c31e7c3dc4d7f5`.
Essa evidência local não é dependência de execução nem arquivo obrigatório do ZIP.

### Decisões e limites registrados antes da autorização de publicação

1. **Antes de publicar o ZIP:** confirmar habilitação/licença Microsoft do
   distribuidor, enquadramento CRT/UCRT e termos apropriados aos destinatários.
   Revisar também o fluxo de preparação AOT proprietária e o uso de dashboards,
   distinguindo contrato e legislação aplicável. Este relatório não dá parecer
   jurídico definitivo nem autorização para distribuir originais Xbox.
2. **SDK completo:** `thirdparty/pe/pe_image.h` continua com copyright Microsoft
   sem licença explícita identificada; as constantes do loader e a fonte/versão
   correspondente dos binutils/Cygwin continuam para revisão. A referência
   histórica SHA256 está delimitada acima. Não foram espelhados esses binários,
   substituídos por terceiros desconhecidos ou reclassificados como BSD.
3. **Se houver distribuição futura de runtime/ferramentas completos:** cumprir
   GPL do decoder e LGPL de FFmpeg/libmspack (fonte correspondente e possibilidade
   de modificação/relinkedição conforme os termos), além das pendências acima.
   Os avisos adicionados não aprovam tal distribuição. Para o candidato atual,
   o gate mantém a exclusão desses binários e a lista estrita dos três EXEs.
4. **Publicação do histórico Git:** resolver a procedência do ícone antigo ou
   decidir uma forma autorizada de publicação do histórico. Não afeta os bytes
   do novo ícone; não foi feita reescrita de histórico.
5. **Compatibilidade para leigos:** Windows limpo continua pendente; esta tarefa
   não promete nova compatibilidade de jogos ou suporte a saves. O teste inicial
   da GUI e os testes focados do novo candidato são registrados abaixo.

O bloqueio histórico `blocked-C10` foi substituído pela decisão expressa do
mantenedor de publicar a primeira prévia, inclusive o histórico, publicamente.
O gate técnico continua ativo e o inventário não declara que todos os componentes
do SDK são livres de pendências. Nenhuma funcionalidade de execução foi modificada.

### Aceitação do novo candidato

Código empacotado: **`d87b5c95f50fafd8058c8ce6f50b844cc238e75a`**.
O commit que registra estes resultados é documental; não altera a revisão
declarada dentro do ZIP. Arquivo:
`.local/packages/c10-provenance/x360-native-0.1.0-preview.7-windows-x64.zip`.
Repetição em `.local/packages/c10-provenance-repeat`, com **3.219.126 bytes e
400 arquivos**, idêntica byte a byte.

- ZIP SHA-256: `ff2dd3e8e1c8e9eaf901f774a34fd015596191d1dacb049e6e7ad85489d89cf1`.
- Payload SHA-256: `2f17390f13b9ddefe6578910d48140ed7d88a275394173e31e749df3237d5088`.
- Builds novas simultâneas: `.local/distribution/4005261fe11449b69b4befaa1a2f9eb5/build`
  e `.local/distribution/5c0c0444dcc34b57b2e1c5dcfb9f7957/build`.

| Verificação | Resultado observado |
|---|---|
| CTest desktop em ambas as builds | 55 casos, 20.628 assertions aprovadas em cada uma. |
| Índices, escopo, originais e licenças | Aprovados nos builds e na extração externa. 88 registros, 18 arquivos de proveniência SDK comparados; três EXEs e nenhuma DLL adicional. Gate exclui conteúdo Xbox e constantes conhecidas do loader, conforme seu escopo documentado. |
| PowerShell 5.1 / testes de pacote | 1 positivo + **21 negativos** aprovados. Novos casos: ICO alterado, ICO com hash recalculado, procedência ausente, texto DXC ausente e registro DXC sem licença. |
| Ícone | Gerador verificado também no PowerShell 5.1; recurso visível na barra de título da GUI. |
| GUI real fora do repositório | Aberta em `C:/Users/fabeg/AppData/Local/Temp/x360-c10-p7b/x360-native-0.1.0-preview.7-windows-x64`. Verificou o pacote, criou `data/environment`, exibiu início com controles habilitados e fechou normalmente. Sem downloads. Os **341 arquivos** do ambiente, inclusive licenças, coincidiram com o índice do payload. |
| Instalação/atualização via modelo real | **43 assertions / 2 casos**, usando ZIP extraído separado (`x360-c10-p7c`), payload anterior preview.6, conteúdo real importado de Castle Crashers/dashboard 17559 e perfis/saves sintéticos. Atualização detectada, confirmação exigida, interrupção/retomada e preservação dos dados verificadas. Não é alegação de funcionamento de save/load em jogos. |
| Recibos C2 reais | **8 assertions** aprovadas para Castle Crashers, dashboard 17559 e Iruka em `.local/build/c5-titles`. Preparações reutilizáveis; sem recompilar jogos nem regenerar codegen. |
| Inicialização console com PATH apenas Windows | Supervisor `--help` retorna 0; extrator imprime uso e retorna 3, conforme contrato existente. |
| Preservação | Código C1/C3, C2, C5, C6 e C4/C11 sem alterações; SDK/LLVM e 38 patches mantidos. Isolamento C5 confirmado pelas duas builds simultâneas. |

As primeiras tentativas da bateria externa revelaram dois problemas do próprio
procedimento de teste, corrigidos antes do aceite: usar o checkout SDK de
desenvolvimento causou rejeição por bytes CRLF diferentes numa licença; a fonte
correta da comparação é o snapshot fixado da distribuição. Abrir a GUI na mesma
extração enquanto os negativos a copiavam encontrou o lock do ambiente criado.
A bateria final foi executada em outra extração imutável, separada da GUI, e passou
integralmente. Não se enfraqueceu o gate nem o lock para contornar essas rejeições.

Logs/evidências locais: `.local/audit/c10-provenance-package.log`,
`c10-provenance-repeat.log`, `c10-p7-extracted-gate.log`, `c10-p7-negatives.log`,
`c10-p7-receipts.log`, `c10-p7-environments.log`, `c10-p7-result.json`,
`c10-p7-gui.json` e `c10-p7-contracts.json`.
Não houve Windows limpo disponível, nova regressão gráfica de jogos, aceite
jurídico, push ou publicação de Release.
