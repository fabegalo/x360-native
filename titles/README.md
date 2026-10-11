# titles/

Configurações e correções por título **e versão de executável**. Vazio: nenhum
título foi preparado ainda.

Convenção planejada (será validada no M1):

```
titles/<title_id>-<nome-curto>/<sha256-do-xex-de-entrada>/
  title.toml        identidade esperada: SHA-256 e tamanho de cada XEX, versão,
                    base_version, media_id, região; revisão do SDK usada
  analysis/*.toml   [functions], [[switch_tables]], [[midasm_hook]] etc. incluídos
                    no manifesto do rexglue
  src/              hooks e correções específicos desse executável
```

O title id serve só para organizar pastas: regiões, atualizações (`patch_delta`)
e mídias diferentes têm executáveis diferentes, e a identidade é sempre o SHA-256
(`x360n identify --json`). Nenhum arquivo de jogo entra aqui; os XEX ficam em
`.local/inputs` e o código gerado em `.local/`.
