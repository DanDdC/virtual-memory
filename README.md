# vm

Implementação da atividade de memória virtual da disciplina de Infraestrutura de Software.

## Compilação

```bash
make
```

O binário gerado é `vm`.

## Uso

```bash
./vm addresses.txt <pagina> <tlb>
```

Algoritmos aceitos: `fifo` e `lru`.

Exemplo:

```bash
./vm addresses.txt lru fifo
```

## Status

Em desenvolvimento.
