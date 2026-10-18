# Release na distribuição final — 18/10/2026

Revisão sobre `d43d3fa`, Windows x64, LLVM 22.1.8 pinado. A distribuição final
deve usar **Release em todos os componentes nativos**. Debug e RelWithDebInfo
continuam disponíveis explicitamente para desenvolvimento/diagnóstico; não são
a configuração de produção. Nenhuma publicação, push ou execução de jogo nesta revisão.

## Diagnóstico

O empacotador `scripts/packaging/package-desktop.ps1` já cria uma árvore limpa
com `CMAKE_BUILD_TYPE=Release`, sem copiar executáveis do ambiente de desenvolvimento.
Launcher, supervisor e extrator são compilados nessa árvore. O runtime C++ do
desktop usa `MultiThreaded` em Release. O workflow verifica o ZIP antes de publicar.
O candidato local `preview.21` contém 167 comandos de compilação, conferidos
com `-O3`, `NDEBUG` e sem CRT Debug.

A lacuna estava na preparação feita depois da instalação: os seis presets padrão
de títulos e o gerador AOT automático selecionavam **RelWithDebInfo**. Portanto,
não era correto afirmar que toda a cadeia de produção exigia Release. O CMake
tampouco recusava explicitamente `X360_DISTRIBUTION=ON` em Debug; a verificação
do ZIP confiava no campo `configuration=Release`, sem conferir as flags registradas.

Os benchmarks anteriores de preflight usaram frontend local Debug, com jogos em
RelWithDebInfo. São evidência dessa configuração específica, não do desempenho
de um pacote final inteiramente Release. Não extrapolar suas latências para ele.

## Correções

- O CMake recusa distribuição fora de desktop-only/Release, incluindo árvores
  multi-config que também permitam Debug/RelWithDebInfo.
- O preset raiz passa a usar Release por padrão. Os seis presets padrão de títulos
  e a geração AOT passam a Release. Os presets
  explícitos de diagnóstico mantêm seu significado.
- O driver força Release nas duas configurações CMake. Uma preparação anterior
  com cache Debug/RelWithDebInfo não pode tomar o atalho de reuso nem pular a
  reconfiguração. O SDK, runtime, código AOT e DLLs desse projeto herdam Release.
- Antes de publicar o recibo, o driver confere os comandos C/C++ expandidos do
  Ninja. Exige `-O3`/`NDEBUG` e recusa flags Debug, CRT Debug, `UNDEBUG` e overrides
  de otimização incompatíveis. O novo helper participa do fingerprint/recibo.
- O gate local do pacote e o verificador final do ZIP conferem essa mesma política
  nas evidências de compilação. JSON declarando Release e índices SHA consistentes
  não bastam para aceitar evidência Debug. Recursos/ASM/custom commands não são
  confundidos com unidades C/C++.

Não há mudança de semântica PPC, renderer, SDK/patches ou conteúdo original.
O staging existente seleciona DLLs dos targets da configuração; o inventário
continua recusando DLLs extras/obsoletas. Não se remove arquivo desconhecido para
forçar a aprovação da migração.

## Validação e limites

Foi compilada a árvore `.local/build/release-validation`, com launcher, supervisor,
extrator, dependências e testes focados em Release. Seus 183 comandos gerados
passaram na política de compilação. Essa árvore de validação não é um ZIP final
nem substitui o gate de empacotamento, licenças ou teste em Windows limpo.

- Política PowerShell: caches ausentes, Debug, RelWithDebInfo, Release, grafia
  incorreta/duplicada; evidência vazia; 11 casos adversos de flags; seis presets
  padrão e preservação dos presets explícitos de diagnóstico.
- Verificador do ZIP: **18 testes Python aprovados**, incluindo arquivos/índices
  autoconsistentes com metadados Release e flags Debug. Chamadas de publicação
  nesses testes são mocks; nenhuma publicação ocorreu.
- Geração AOT executada no host Release: **4 casos/94 asserções aprovadas**.
- Gates de lançamento sob `NDEBUG`: **3 casos/94 asserções aprovadas**; somente
  fixtures sintéticos, sem executar jogos.
- CMake real: distribuição Debug e RelWithDebInfo recusadas. Configuração Release
  aceita com identidade de payload sintética de teste, sem build/pacote desse
  fixture; árvore multi-config misturando Debug/Release também exercitada.

Castle foi configurado numa árvore separada, `.local/build/release-title-validation`,
sem construir ou executar o título. Os **766 comandos** gerados passaram: 96
unidades de runtime, 36 do Xenos, 21 do título e 613 de SDK/dependências. O caminho
exato usado pelo driver (`ninja -t compdb -x`) também passou, com 1.095 arestas no
grafo completo. A primeira integração do gate encontrou arestas phony cujo input
era `.cpp`; corrigido para conferir os produtores reais de objetos/PCH nesse
formato. Há regressão para ignorar a aresta phony sem aceitar um objeto Debug.
O verificador de `compile_commands.json` do pacote continua exigindo evidência
de compilação, sem esse filtro próprio do grafo Ninja.

O SDK já adiciona `-gline-tables-only` a alguns fontes Release para diagnóstico;
isso não os converte em Debug. A configuração efetiva é Release com `-O3`,
`NDEBUG` e CRT de Release. Não foram alteradas as opções existentes de tracing
ou contadores; não confundir disponibilidade de instrumentação com build Debug.

A primeira execução do teste AOT usou TEMP longo e falhou ao escrever o projeto;
com TEMP curto em `.local/rt`, passou. O log inicial foi mantido. Uma execução dos
testes Python dentro do sandbox não conseguiu acessar seu TEMP; o rerun fora
desse TEMP, no workspace, passou. Nenhum desses erros foi transformado em sucesso.

Evidência em `.local/evidence/release-configuration-2026-10-18/`: logs dos testes,
configurações aceitas/recusadas e inspeção do grafo do título/SDK. Os binários
locais antigos Debug/RelWithDebInfo não foram renomeados como Release nem os quatro
jogos foram reconstruídos silenciosamente. Após esta mudança, a preparação normal
deve migrar os produtos e emitir novos recibos antes de considerá-los prontos.
