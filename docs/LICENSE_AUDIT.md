# C10 — licenças e distribuição

Auditoria de 13/10/2026. **Publicação ainda bloqueada**, pelos itens abaixo.
Não houve push, publicação, mudança de visibilidade ou reescrita de histórico.
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

## Pendências que bloqueiam publicação

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
