# vm

Implementação da atividade de memória virtual da disciplina de Infraestrutura de Software.

O programa traduz endereços lógicos de 16 bits usando páginas de 256 bytes, tabela de páginas com 256 entradas, memória física com 128 frames e TLB com 16 entradas. A substituição de página e a substituição da TLB podem usar FIFO ou LRU. A busca na TLB usa uma thread por entrada.

## Requisitos

- `gcc`
- `make`
- `pthreads`
- Ambiente Linux, macOS ou WSL

Os arquivos `addresses.txt` e `BACKING_STORE.bin` fornecidos pelo professor devem estar no diretório de execução.

## Compilação

```bash
make
```

O binário gerado é `vm`.

Para remover o binário:

```bash
make clean
```

A compilação usa `-Wall -Wextra -Werror -std=c11 -pedantic -O2 -pthread`.

## Uso

```bash
./vm <arquivo_de_enderecos> <algoritmo_de_pagina> <algoritmo_da_tlb>
```

Algoritmos aceitos: `fifo` e `lru`.

Exemplo:

```bash
./vm addresses.txt lru fifo
```

Nesse exemplo, a página usa LRU e a TLB usa FIFO.

## Entrada

`addresses.txt` deve conter um endereço lógico decimal por linha, no intervalo de 0 a 65535. Espaços e tabulações nas bordas são aceitos. Linhas vazias, valores negativos, valores acima de 65535 e texto inválido são rejeitados.

`BACKING_STORE.bin` deve ter exatamente 65536 bytes, correspondentes a 256 páginas de 256 bytes.

## Saída

O programa grava `correct.txt` com uma linha por endereço traduzido:

```text
Virtual address: 16916 TLB: 0 Physical address: 20 Value: 0
```

Depois das traduções, são gravadas as estatísticas:

```text
Number of Translated Addresses = 1000
Page Faults = 539
Page Fault Rate = 0.539
TLB Hits = 54
TLB Hit Rate = 0.054
```

O campo `TLB` é o índice do slot usado na TLB, de 0 a 15. Em builds sem TLB, o campo é `-1`. O campo `Value` é lido como `signed char`.

A saída é escrita diretamente em `correct.txt`, com checagem de abertura, escrita e fechamento. O programa não usa `rename` nem `unlink`; se a escrita falhar, pode restar um `correct.txt` parcial e o programa termina com código 1.

O arquivo `correct.txt` fornecido pelo professor usa o mesmo nome da saída. Antes da primeira execução, preserve a referência:

```bash
cp correct.txt /tmp/correct.reference.txt
./vm addresses.txt lru fifo
cmp /tmp/correct.reference.txt correct.txt
```

## Resultados esperados

| Página | TLB | Page faults | TLB hits |
|---|---|---|---|
| FIFO | FIFO | 538 | 54 |
| FIFO | LRU | 538 | 55 |
| LRU | FIFO | 539 | 54 |
| LRU | LRU | 539 | 55 |

A combinação `lru fifo` reproduz exatamente o `correct.txt` de referência.

## Algoritmos

- Tabela de páginas com 256 entradas e memória física com 128 frames.
- Os frames são preenchidos de 0 a 127; depois disso, a política escolhida seleciona a vítima.
- FIFO de página usa a ordem de entrada dos frames.
- LRU de página usa o instante do último acesso.
- TLB com 16 entradas, também com FIFO ou LRU.
- Ao reutilizar um frame, a PTE da página vítima e as entradas antigas da TLB são invalidadas.
- A busca na TLB cria 16 threads por acesso, uma para cada entrada, e usa `atomic_int` para registrar o slot encontrado. As threads são aguardadas com `pthread_join` antes de qualquer alteração na TLB.

## Builds de validação

O build padrão é o da entrega: 128 frames, TLB com 16 entradas e busca com threads.

Os builds abaixo servem para validar os fluxos das rubricas e não são o padrão:

```bash
make clean && make CPPFLAGS="-DFRAME_COUNT=256"
make clean && make CPPFLAGS="-DTLB_USE_THREADS=0"
make clean && make CPPFLAGS="-DENABLE_TLB=0"
make clean && make CPPFLAGS="-DENABLE_TLB=0 -DFRAME_COUNT=256"
make clean && make
```

- `FRAME_COUNT=256`: todos os endereços cabem na memória física; são 244 page faults.
- `TLB_USE_THREADS=0`: busca sequencial na TLB, com saída idêntica à versão com threads.
- `ENABLE_TLB=0`: fluxo sem TLB, com `TLB: -1` e zero TLB hits. Os page faults dependem apenas da política de página.
- Sem TLB e 128 frames: 539 faults com LRU e 538 com FIFO.
- Sem TLB e 256 frames: 244 faults.

Sempre rode `make clean` antes de trocar as flags; o `make` não detecta mudança de `CPPFLAGS` sozinho.

## Erros tratados

- Número de argumentos diferente de 4.
- Algoritmo de página ou de TLB diferente de `fifo` e `lru`.
- Arquivo de endereços ausente, vazio, malformado ou com valor fora de 0 a 65535.
- Backing store ausente, truncado ou com mais de 65536 bytes.
- Falha ao abrir, escrever ou fechar o arquivo de saída.

Em todos esses casos o programa imprime uma mensagem em `stderr` e termina com código 1; em falha de escrita, pode restar um `correct.txt` parcial.

## Estrutura

- `vm.c`: leitura e validação das entradas, tabela de páginas, frames, TLB, threads e escrita da saída.
- `Makefile`: alvos `all` e `clean`.
- `README.md`: esta documentação.
- `addresses.txt`, `BACKING_STORE.bin` e `correct.txt`: arquivos fornecidos ou gerados; não fazem parte do código.

## Observações

- A criação de 16 threads por acesso tem custo: no ambiente testado, a versão threaded levou cerca de 0,8 s nesta validação (a primeira medição foi 1,3 s) e a sequencial cerca de 0,005 s para 1000 endereços. O objetivo da thread é simular a busca paralela exigida pelo enunciado, não reduzir o tempo total.
- O build padrão com `lru fifo` deve ser comparado com a referência usando `cmp`; os demais fluxos têm estatísticas próprias.

## Ambiente testado

- Ubuntu 26.04 LTS em WSL2
- gcc 15.2.0
- GNU Make 4.4.1
- Valgrind para verificação de memória
