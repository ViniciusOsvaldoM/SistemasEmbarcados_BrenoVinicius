    CEFET - MG                                                  09/07/26
| Disciplina  | Alunos| Professor|
| ----------------- | --- | --- |
| Sistemas Embarcados| Breno Guimarães e Vinícius Osvaldo | Túlio Charles |


# Trabalho Final - Sistemas Embarcados

Criação de um "jogo da cobrinha", exibido no display e controlado pelo celular, utilizando a biblioteca LVGL e a comunicação MQTT. O código apresenta duas implementações estudadas na disciplina: o MQTT e o uso do Display I2C. 

## Funcionamento

### Tarefas e Filas
O programa apresenta duas tarefas: `example_lvgl_port_task` e `example_display_port_task` para o funcionamento do display. Também há uma fila para o envio dos comandos dos botões, a `fila_I2C`.

### MQTT
Foram criados quatro botões para o envio dos comandos: up, down, left, right, com tópicos MQTT individuais de mesmos nomes dos botões. Conforme eles são pressionados, seu tópico é adquirido, armazenado na variável `direcao` e enviado por meio da `fila_I2C` para a tarefa do display.

### Display

1. Criação e posição dos objetos: 

    Foram criados dois objetos retangulares: a cobra e a semente, ambos do tipo `lv_obj_t` por meio da função `lv_obj_create(scr)`. Conforme a cobra encosta na semente, seu tamanho horizontal (`largura`) é aumentado, e é gerada uma nova semente em uma posição aleatória.para posicionar os objetos, foi utilizada a função `lv_obj_set_pos()`.

    Vale ressaltar que o centro da tela corresponde à posição (0,0), que possuem valores positivos à direita e abaixo, enquanto as posições acima e à esquerda possuem valores negativos. Tendo em vista o display de 128x64 pixels, as extremidades da tela estarão dentro dos seguintes intervalos:
    - Horizontal: x = [-64, 64]
    - Vertical: y = [-32, 32]


2. Movimento

    Foi possível movimentar a cobra dentro de um loop while() infinito. Para isso, a posição do objeto retangular é incrementada continuamente com um valor de 5 pixels. Há um delay de 200ms entre os incrementos, que define uma velocidade razoável para a cobra. 
    
    O comando da `direcao` é recebido pela `fila_I2C` e comparado com as quatro strings dos tópicos. Então, realiza as seguintes ações:
    - **Up:**    decrementa eixo y em 5 pixels.
    - **Down:**  incrementa eixo y em 5 pixels.
    - **Left:**  decrementa eixo x em 5 pixels.
    - **Right:** incrementa eixo x em 5 pixels.

    Quando a cobra chega ao final da tela, conforme a direção do seu movimento, irá retornar para o início, continuando o movimento, permanecendo sempre dentro do intervalo citado anteriormente.

3. Colisão com a semente

    Durante o movimento da cobra, foi feita a detecção da colisão por meio de comparações. As coordenads dos objetos foram obtidas utilizando a função `lv_obj_get_coords`, que armazena sua área.

    Esta função retorna as posições *x* e *y* inicial e final do objeto, como mostrado no protótipo:
    ```
    typedef struct {
        lv_coord_t x1; // Left coordinate
        lv_coord_t y1; // Top coordinate
        lv_coord_t x2; // Right coordinate
        lv_coord_t y2; // Bottom coordinate
    } lv_area_t;
    ```
    Assim, para detectar a colisão deve ser satisfeita a seguinte condição:

    ```
    if (!(area_cobra.x2 < area_semente.x1 || 
              area_cobra.x1 > area_semente.x2 || 
              area_cobra.y2 < area_semente.y1 || 
              area_cobra.y1 > area_semente.y2))
    ```

## Resultados 
O programa apresentou um resultado inicial básico e limitado, mas que pode servir como base para a aplicação de um jogo verdadeiro. O comando dos botões pelo aplicativo apresentou delays pequenos, esperados pela comunicação MQTT, que não compreteram tanto o uso

Não foi possível realizar a curva da cobra durante a mudança das dirações, o que causou algumas limitações no jogo. Como possível solução, poderia ser criado um vetor de objetos, e trabalhado o movimento individual ou recursivo deles, a fim de simular a dobra da cobra, representada pelo retângulo.

## Conclusões

Com este trabalho, foi possível aplicar os conceitos aprendidos na disciplina e aprender novos usos dos componetes. Também percebe-se que, apesar das limitações existentes, é possível implementar projetos funcionais utilizando os conhecimentos adquiridos ao longo do semestre.