# Integração de build

| Diretório | Responsabilidade |
|---|---|
| `sdk/` | Backports condicionados à revisão fixada e verificação dos patches |
| `build/` | Isolamento do Ninja e cópia de DLLs por produtor/destino |
| `runtime/` | Contratos PPC, identidade dos originais e manifestos Windows |
| `distribution/` | Guard de fontes do pacote desktop |

`x360n_sdk_backports.cmake` na raiz encaminha para `sdk/`: projetos AOT locais
gerados anteriormente continuam encontrando o ponto de entrada. Não remover
enquanto esses projetos existirem. Os projetos novos usam os módulos organizados.

Includes em `tests/cmake/` mantêm o escopo de `tests/`; não são subprojetos e não
alteram os diretórios de saída. O SDK/submódulo, o lock e seus patches não foram
movidos ou substituídos.
