# Testes

Os diretórios acompanham os componentes: `desktop/`, `supervisor/`, `content/`,
`native/` e `avatar/`. `sdkcheck/` verifica contratos de patches do SDK;
`local/` lê originais fornecidos pelo mantenedor; `ppc_smoke/` e `ppc_contract/`
usam programas sintéticos. `build_isolation/`, `performance/` e `packaging/`
verificam as ferramentas correspondentes.

`CMakeLists.txt` orquestra os grupos em `cmake/` por include, preservando o escopo
de diretório, os nomes, labels, timeouts e caminhos dos executáveis. Os testes
desktop ficam em `desktop/targets.cmake`, incluído no escopo original de
`src/desktop` para preservar as localizações dos binários e o modo desktop-only.

Usar filtros/uma lista explícita para as regressões afetadas, após build aprovado.
Não executar automaticamente a suíte completa do SDK nem os testes visuais
`local.iruka.regression` e `local.motocross.shared-xam`. Não remover verificações
ou tratar um skip como sucesso funcional. Fixtures sintéticos ficam em diretórios
temporários isolados; preservar perfis, saves e catálogos reais.

`x360n_user_mode_page_probe` é um diagnóstico explícito, fora de ALL e CTest.
Consulta o export real de PTE em memória sintética; não carrega nem executa
originais. Saída 78 registra incompatibilidade com o teste de atributos do
consumidor de modo de usuário, não aprovação. Comando e limites no
[relatório NetCF](../docs/reports/charlie-netcf-2026-10-18.md).
