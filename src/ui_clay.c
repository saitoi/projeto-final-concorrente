/**
 * @file ui_clay.c
 * @brief Interface visual usando Clay UI Library
 * @details Interface com busca, configurações e loading
 */

 // Definir CLAY_IMPLEMENTATION antes de incluir clay.h (apenas uma vez no projeto)
#define CLAY_IMPLEMENTATION
#include "clay.h"

#include <raylib.h>
#include <rlgl.h>  // Para rlPushMatrix, rlPopMatrix, rlTranslatef
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

// Headers do backend TF-IDF
#include "../include/sqlite_helper.h"
#include "../include/preprocess_query.h"
#include "../include/hash_t.h"
#include "../include/file_io.h"

// Variáveis globais do backend
int VERBOSE = 0;  // Flag de verbosidade desabilitada para UI
extern hash_t *global_stopwords;  // Definida em file_io.c

// Variáveis globais do TF-IDF (definidas aqui para a UI)
hash_t **global_tf = NULL;      // Array de hashes TF (Term Frequency) por documento
hash_t *global_idf = NULL;      // Hash IDF (Inverse Document Frequency) global
double *global_doc_norms = NULL; // Normas dos documentos
long int global_entries = 0;    // Número total de documentos processados

// Fonte global
Font pixelFont;

// ============================================================================
// PALETA DE CORES
// ============================================================================
const Clay_Color COLOR_BG_DARK = { 23, 23, 28, 255 };
const Clay_Color COLOR_BG_CARD = { 35, 35, 45, 255 };
const Clay_Color COLOR_PRIMARY = { 139, 92, 246, 255 };
const Clay_Color COLOR_PRIMARY_HOVER = { 124, 58, 237, 255 };
const Clay_Color COLOR_SECONDARY = { 59, 130, 246, 255 };
const Clay_Color COLOR_SUCCESS = { 34, 197, 94, 255 };
const Clay_Color COLOR_TEXT = { 248, 250, 252, 255 };
const Clay_Color COLOR_TEXT_DIM = { 148, 163, 184, 255 };
const Clay_Color COLOR_BORDER = { 51, 65, 85, 255 };
const Clay_Color COLOR_INPUT_BG = { 45, 45, 55, 255 };

// ============================================================================
// ESTADO DA APLICAÇÃO
// ============================================================================

// Estrutura para armazenar resultados da busca
typedef struct {
    long int docId;
    char title[128];
    float score;
} SearchResult;

typedef struct {
    char searchQuery[256];
    int selectedDbIndex;
    int numThreads;
    int maxDocuments;  // Número máximo de documentos a processar
    bool isProcessing;
    float processingProgress;
    char statusMessage[128];
    bool inputActive;  // Se o campo de busca está ativo
    int cursorPosition;  // Posição do cursor no texto
    float scrollOffset;  // Offset de scroll vertical

    // Resultados da busca
    SearchResult results[10];  // Top 10 resultados
    int numResults;
    double searchTime;  // Tempo de busca em segundos
    int searchThreads;  // Threads usadas na última busca
    int topK;  // Quantidade de resultados a retornar (1-10)
    float resultsScrollOffset;  // Scroll dentro da seção de resultados
} AppState;

AppState appState = {
    .searchQuery = "",
    .selectedDbIndex = 0,
    .numThreads = 4,
    .maxDocuments = 97549,  // Todos os documentos por padrão
    .isProcessing = false,
    .processingProgress = 0.0f,
    .statusMessage = "Pronto",
    .inputActive = false,
    .cursorPosition = 0,
    .scrollOffset = 0.0f,
    .numResults = 0,
    .searchTime = 0.0,
    .searchThreads = 0,
    .topK = 10,  // Padrão: retornar top 10 resultados
    .resultsScrollOffset = 0.0f
};

// Configuração dos bancos de dados disponíveis
typedef struct {
    const char *path;
    const char *table;
    const char *displayName;
    int totalDocs;
} DatabaseConfig;

DatabaseConfig databases[] = {
    {"data/wiki-small.db", "sample_articles", "wiki-small", 97549},
    {"data/food-reviews.db", "sample_articles", "food-reviews", 568454}
};
const int NUM_DATABASES = 2;

/**
 * @brief Cria um Clay_String a partir de uma string C
 * @param str String C a ser convertida
 * @return Clay_String dinâmico
 */
Clay_String MakeClayString(const char *str) {
    return (Clay_String) {
        .chars = str,
            .length = (int) strlen(str),
            .isStaticallyAllocated = false
    };
}

/**
 * @brief Função de medição de texto para Clay
 * @param text Slice de texto a ser medido
 * @param config Configuração do elemento de texto
 * @param userData Dados do usuário (não usado)
 * @return Dimensões do texto
 */
Clay_Dimensions MeasureTextForClay(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    (void) userData; // Suprimir warning de parâmetro não usado

    // Precisamos converter o slice para string C temporária
    char *tempString = (char *) malloc(text.length + 1);
    memcpy(tempString, text.chars, text.length);
    tempString[text.length] = '\0';

    // Usar espaçamento fixo para melhorar legibilidade
    float spacing = config->letterSpacing > 0 ? config->letterSpacing : 1.0f;

    // Usar fonte customizada se carregada, senão usar default
    Font font = (pixelFont.texture.id > 0) ? pixelFont : GetFontDefault();

    Vector2 measured = MeasureTextEx(
        font,
        tempString,
        config->fontSize,
        spacing
    );

    free(tempString);

    return (Clay_Dimensions) {
        .width = measured.x,
            .height = measured.y
    };
}/**
 * @brief Handler de erros do Clay
 * @param errorData Dados do erro
 */
void HandleClayErrors(Clay_ErrorData errorData) {
    printf("[CLAY ERROR] %s\n", errorData.errorText.chars);
}

// ============================================================================
// FUNÇÕES DE BUSCA TF-IDF
// ============================================================================

/**
 * @brief Estrutura para resultado de busca com score
 */
typedef struct {
    long int docId;
    double score;
} ScoredDocument;

/**
 * @brief Comparador para qsort (ordem decrescente de score)
 */
int compare_scores(const void *a, const void *b) {
    ScoredDocument *doc_a = (ScoredDocument *) a;
    ScoredDocument *doc_b = (ScoredDocument *) b;
    if (doc_b->score > doc_a->score) return 1;
    if (doc_b->score < doc_a->score) return -1;
    return 0;
}

/**
 * @brief Executa busca TF-IDF real no banco de dados
 * @param db_path Caminho do banco de dados
 * @param table_name Nome da tabela
 * @param query Consulta do usuário
 * @param num_threads Número de threads
 * @param max_docs Número máximo de documentos a processar (0 = todos)
 * @param total_docs_in_db Total de documentos no banco (para path dos modelos)
 * @param top_k_param Quantidade de resultados a retornar (1-10)
 * @param results_out Array de resultados (alocado pela função)
 * @return Número de documentos encontrados
 */
int perform_tfidf_search(const char *db_path, const char *table_name,
    const char *query, int num_threads, int max_docs, int total_docs_in_db,
    int top_k_param, SearchResult *results_out) {

    // Verifica se stopwords foram carregadas
    if (global_stopwords == NULL) {
        printf("[ERRO] Stopwords não carregadas!\n");
        return 0;
    }

    // Constrói caminhos dos arquivos de modelo
    // Formato: models/{tipo}_{tabela}_{numDocs}.bin
    char idf_path[512];
    char tf_path[512];
    char norms_path[512];

    snprintf(idf_path, sizeof(idf_path), "models/idf_%s_%d.bin", table_name, total_docs_in_db);
    snprintf(tf_path, sizeof(tf_path), "models/tf_%s_%d.bin", table_name, total_docs_in_db);
    snprintf(norms_path, sizeof(norms_path), "models/doc_norms_%s_%d.bin", table_name, total_docs_in_db);

    printf("[INFO] Procurando modelos:\n");
    printf("  IDF: %s\n", idf_path);
    printf("  TF: %s\n", tf_path);
    printf("  Norms: %s\n", norms_path);

    // Carrega IDF
    hash_t *global_idf = load_hash(idf_path);
    if (!global_idf) {
        printf("[ERRO] Não foi possível carregar IDF de %s\n", idf_path);
        return 0;
    }

    // Pré-processar query
    hash_t *query_tf = NULL;
    double query_norm = 0.0;
    int result = preprocess_query(query, global_idf, &query_tf, &query_norm);

    // Verifica se preprocessamento foi bem-sucedido
    if (result != 0 || query_tf == NULL || query_tf->size == 0 || query_norm == 0.0) {
        printf("[AVISO] Query não gerou termos válidos após pré-processamento\n");
        printf("[DEBUG] result=%d, query_tf=%p, size=%zu, norm=%f\n",
            result, (void *) query_tf, query_tf ? query_tf->size : 0, query_norm);
        hash_free(global_idf);
        if (query_tf) hash_free(query_tf);
        return 0;
    }

    // Carregar TF de todos os documentos
    long int num_docs = 0;
    hash_t **global_tf = load_hash_array(tf_path, &num_docs);
    if (global_tf == NULL || num_docs == 0) {
        printf("[ERRO] Não foi possível carregar TF de %s\n", tf_path);
        hash_free(global_idf);
        hash_free(query_tf);
        return 0;
    }

    // Limitar número de documentos se especificado
    long int docs_to_process = num_docs;
    if (max_docs > 0 && max_docs < num_docs) {
        docs_to_process = max_docs;
        printf("[INFO] Limitando busca a %d de %ld documentos totais\n", max_docs, num_docs);
    }

    // Carregar normas dos documentos
    long int num_norms = 0;
    double *global_doc_norms = load_doc_norms(norms_path, &num_norms);
    if (global_doc_norms == NULL || num_norms != num_docs) {
        printf("[ERRO] Não foi possível carregar normas de %s\n", norms_path);
        hash_free(global_idf);
        hash_free(query_tf);
        for (long int i = 0; i < num_docs; i++) {
            if (global_tf[i]) hash_free(global_tf[i]);
        }
        free(global_tf);
        return 0;
    }

    // Calcular similaridades (apenas para docs_to_process)
    double *similarities = compute_similarities(
        query_tf, query_norm, global_tf, global_doc_norms, docs_to_process, num_threads
    );

    if (similarities == NULL) {
        printf("[ERRO] Falha ao calcular similaridades\n");
        hash_free(global_idf);
        hash_free(query_tf);
        for (long int i = 0; i < num_docs; i++) {
            if (global_tf[i]) hash_free(global_tf[i]);
        }
        free(global_tf);
        free(global_doc_norms);
        return 0;
    }

    // Criar array de documentos com scores
    ScoredDocument *scored_docs = (ScoredDocument *) malloc(docs_to_process * sizeof(ScoredDocument));
    for (long int i = 0; i < docs_to_process; i++) {
        scored_docs[i].docId = i;
        scored_docs[i].score = similarities[i];
    }

    // Ordenar por score decrescente
    qsort(scored_docs, docs_to_process, sizeof(ScoredDocument), compare_scores);

    // Pegar top-k (conforme solicitado pelo usuário)
    int top_k = (docs_to_process < top_k_param) ? docs_to_process : top_k_param;
    long int *top_doc_ids = (long int *) malloc(top_k * sizeof(long int));
    for (int i = 0; i < top_k; i++) {
        top_doc_ids[i] = scored_docs[i].docId;
    }

    // Buscar textos dos documentos
    char **doc_texts = get_documents_by_ids(db_path, table_name, top_doc_ids, top_k);

    // Preencher resultados
    int num_results = 0;
    printf("[DEBUG] top_k = %d\n", top_k);
    for (int i = 0; i < top_k; i++) {
        if (doc_texts && doc_texts[i]) {
            results_out[num_results].docId = scored_docs[i].docId;
            results_out[num_results].score = (float) scored_docs[i].score;

            // Copiar início do texto (primeiros 100 chars, garantir null-termination)
            int text_len = strlen(doc_texts[i]);
            int copy_len = (text_len < 100) ? text_len : 100;

            strncpy(results_out[num_results].title, doc_texts[i], copy_len);
            results_out[num_results].title[copy_len] = '\0';  // Garantir terminação

            // Substituir caracteres problemáticos por espaços
            // Mantém apenas caracteres ASCII imprimíveis (32-126)
            for (int j = 0; j < copy_len; j++) {
                unsigned char c = (unsigned char) results_out[num_results].title[j];
                if (c < 32 || c > 126) {
                    results_out[num_results].title[j] = ' ';
                }
            }

            printf("[DEBUG] Resultado %d: docId=%ld, score=%.4f, title=%.50s...\n",
                num_results, scored_docs[i].docId, scored_docs[i].score, results_out[num_results].title);
            num_results++;
        } else {
            printf("[DEBUG] Resultado %d: doc_texts NULL ou doc_texts[%d] NULL\n", i, i);
        }
    }
    printf("[DEBUG] Total de resultados preenchidos: %d\n", num_results);    // Liberar memória
    hash_free(global_idf);
    hash_free(query_tf);
    for (long int i = 0; i < num_docs; i++) {
        if (global_tf[i]) hash_free(global_tf[i]);
    }
    free(global_tf);
    free(global_doc_norms);
    free(similarities);
    free(scored_docs);
    free(top_doc_ids);
    if (doc_texts) {
        for (int i = 0; i < top_k; i++) {
            if (doc_texts[i]) free(doc_texts[i]);
        }
        free(doc_texts);
    }

    return num_results;
}


// ============================================================================
// FUNÇÕES DE RENDERIZAÇÃO
// ============================================================================

/**
 * @brief Renderiza um comando de retângulo
 */
void RenderRectangle(Clay_RenderCommand *renderCommand) {
    Clay_RectangleRenderData *data = &renderCommand->renderData.rectangle;
    Clay_BoundingBox bbox = renderCommand->boundingBox;

    Color color = {
        data->backgroundColor.r,
        data->backgroundColor.g,
        data->backgroundColor.b,
        data->backgroundColor.a
    };

    // Desenhar retângulo com cantos arredondados se necessário
    if (data->cornerRadius.topLeft > 0) {
        DrawRectangleRounded(
            (Rectangle) {
            bbox.x, bbox.y, bbox.width, bbox.height
        },
            data->cornerRadius.topLeft / (bbox.width < bbox.height ? bbox.width : bbox.height),
            8,
            color
        );
    } else {
        DrawRectangle(bbox.x, bbox.y, bbox.width, bbox.height, color);
    }
}

/**
 * @brief Renderiza um comando de texto
 */
void RenderText(Clay_RenderCommand *renderCommand) {
    Clay_TextRenderData *data = &renderCommand->renderData.text;
    Clay_BoundingBox bbox = renderCommand->boundingBox;

    Color color = {
        data->textColor.r,
        data->textColor.g,
        data->textColor.b,
        data->textColor.a
    };

    // Converter slice para string C
    char *tempString = (char *) malloc(data->stringContents.length + 1);
    memcpy(tempString, data->stringContents.chars, data->stringContents.length);
    tempString[data->stringContents.length] = '\0';

    // Usar espaçamento fixo para melhorar legibilidade
    float spacing = data->letterSpacing > 0 ? data->letterSpacing : 1.0f;

    // Usar fonte customizada se carregada, senão usar default
    Font font = (pixelFont.texture.id > 0) ? pixelFont : GetFontDefault();

    DrawTextEx(
        font,
        tempString,
        (Vector2) {
        bbox.x, bbox.y
    },
        data->fontSize,
        spacing,
        color
    );

    free(tempString);
}

/**
 * @brief Renderiza um comando de borda
 */
void RenderBorder(Clay_RenderCommand *renderCommand) {
    Clay_BorderRenderData *data = &renderCommand->renderData.border;
    Clay_BoundingBox bbox = renderCommand->boundingBox;

    Color color = {
        data->color.r,
        data->color.g,
        data->color.b,
        data->color.a
    };

    // Desenhar bordas
    if (data->width.left > 0) {
        DrawRectangle(bbox.x, bbox.y, data->width.left, bbox.height, color);
    }
    if (data->width.right > 0) {
        DrawRectangle(bbox.x + bbox.width - data->width.right, bbox.y,
            data->width.right, bbox.height, color);
    }
    if (data->width.top > 0) {
        DrawRectangle(bbox.x, bbox.y, bbox.width, data->width.top, color);
    }
    if (data->width.bottom > 0) {
        DrawRectangle(bbox.x, bbox.y + bbox.height - data->width.bottom,
            bbox.width, data->width.bottom, color);
    }
}

/**
 * @brief Cria a UI do projeto TF-IDF
 */
void CreateUI(void) {
    // Container de scroll externo (viewport)
    CLAY(CLAY_ID("ScrollContainer"), {
        .layout = {
            .sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = COLOR_BG_DARK,
        .clip = {
            .vertical = true,
            .horizontal = false,
            .childOffset = {0, appState.scrollOffset}
        }
        }) {

        // Container interno com conteúdo
        CLAY(CLAY_ID("MainContainer"), {
            .layout = {
                .sizing = {.width = CLAY_SIZING_GROW(0)},
                .padding = CLAY_PADDING_ALL(16),
                .childGap = 12,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            }
            }) {
            // ========== HEADER ==========
            CLAY(CLAY_ID("Header"), {
            .layout = {
                .sizing = {.width = CLAY_SIZING_GROW(0)},
                .padding = CLAY_PADDING_ALL(16),
                .childGap = 6,
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childAlignment = {.x = CLAY_ALIGN_X_CENTER}
            },
            .backgroundColor = COLOR_PRIMARY,
            .cornerRadius = CLAY_CORNER_RADIUS(12)
                }) {
                CLAY_TEXT(CLAY_STRING("TF-IDF Document Search System"), CLAY_TEXT_CONFIG({
                    .fontSize = 42,
                    .letterSpacing = 1.5f,
                    .textColor = COLOR_TEXT
                    }));
                CLAY_TEXT(CLAY_STRING("Processamento Paralelo com Pthreads"), CLAY_TEXT_CONFIG({
                    .fontSize = 20,
                    .letterSpacing = 1.0f,
                    .textColor = COLOR_TEXT_DIM
                    }));
            }

            // ========== BARRA DE BUSCA ==========
            CLAY(CLAY_ID("SearchSection"), {
                .layout = {
                    .sizing = {.width = CLAY_SIZING_GROW(0)},
                    .padding = CLAY_PADDING_ALL(12),
                    .childGap = 8,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = COLOR_BG_CARD,
                .cornerRadius = CLAY_CORNER_RADIUS(12)
                }) {
                CLAY_TEXT(CLAY_STRING("Buscar Documentos"), CLAY_TEXT_CONFIG({
                    .fontSize = 24,
                    .letterSpacing = 1.0f,
                    .textColor = COLOR_TEXT
                    }));

                // Campo de busca (clicável)
                CLAY(CLAY_ID("SearchInput"), {
                    .layout = {
                        .sizing = {.width = CLAY_SIZING_GROW(0)},
                        .padding = CLAY_PADDING_ALL(12),
                        .childGap = 8,
                        .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}
                    },
                    .backgroundColor = appState.inputActive ? COLOR_PRIMARY : COLOR_INPUT_BG,
                    .cornerRadius = CLAY_CORNER_RADIUS(8),
                    .border = {
                        .width = {2, 2, 2, 2, 0},
                        .color = appState.inputActive ? COLOR_PRIMARY_HOVER : COLOR_BORDER
                    }
                    }) {
                    CLAY_TEXT(CLAY_STRING("Q"), CLAY_TEXT_CONFIG({
                        .fontSize = 36,
                        .letterSpacing = 1.5f,
                        .textColor = appState.inputActive ? COLOR_TEXT : COLOR_PRIMARY
                        }));

                    // Mostrar query atual ou placeholder
                    if (strlen(appState.searchQuery) > 0) {
                        // Criar buffer estático local para o texto
                        static char displayText[270];
                        if (appState.inputActive && ((int) (GetTime() * 2) % 2 == 0)) {
                            snprintf(displayText, sizeof(displayText), "%s|", appState.searchQuery);
                        } else {
                            snprintf(displayText, sizeof(displayText), "%s", appState.searchQuery);
                        }
                        CLAY_TEXT(MakeClayString(displayText), CLAY_TEXT_CONFIG({
                            .fontSize = 30,
                            .letterSpacing = 1.5f,
                            .textColor = (Clay_Color){255, 255, 255, 255}
                            }));
                    } else {
                        if (appState.inputActive && ((int) (GetTime() * 2) % 2 == 0)) {
                            CLAY_TEXT(CLAY_STRING("|"), CLAY_TEXT_CONFIG({
                                .fontSize = 30,
                                .letterSpacing = 1.5f,
                                .textColor = (Clay_Color){255, 255, 255, 255}
                                }));
                        } else {
                            CLAY_TEXT(CLAY_STRING("Digite sua consulta aqui... (ex: machine learning algorithms)"), CLAY_TEXT_CONFIG({
                                .fontSize = 24,
                                .letterSpacing = 1.0f,
                                .textColor = COLOR_TEXT_DIM
                                }));
                        }
                    }

                    // Detectar clique no campo
                    if (Clay_Hovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                        appState.inputActive = true;
                    }
                }

                CLAY_TEXT(CLAY_STRING("Clique no campo para digitar | ENTER para confirmar | ESC para cancelar"), CLAY_TEXT_CONFIG({
                    .fontSize = 18,
                    .letterSpacing = 0.8f,
                    .textColor = COLOR_TEXT_DIM
                    }));
            }

            // ========== CONFIGURAÇÕES (2 colunas) ==========
            CLAY(CLAY_ID("ConfigRow"), {
                .layout = {
                    .sizing = {.width = CLAY_SIZING_GROW(0)},
                    .childGap = 12
                }
                }) {
                // Banco de Dados
                CLAY(CLAY_ID("DbConfig"), {
                    .layout = {
                        .sizing = {.width = CLAY_SIZING_GROW(0)},
                        .padding = CLAY_PADDING_ALL(12),
                        .childGap = 8,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                    },
                    .backgroundColor = COLOR_BG_CARD,
                    .cornerRadius = CLAY_CORNER_RADIUS(12)
                    }) {
                    CLAY_TEXT(CLAY_STRING("Banco de Dados"), CLAY_TEXT_CONFIG({
                        .fontSize = 27,
                        .letterSpacing = 1.2f,
                        .textColor = COLOR_TEXT
                        }));

                    // Container horizontal para botões + banco
                    CLAY(CLAY_ID("DbControls"), {
                        .layout = {
                            .sizing = {.width = CLAY_SIZING_GROW(0)},
                            .childGap = 12,
                            .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
                        }
                        }) {

                        // Botão < (anterior)
                        CLAY(CLAY_ID("DbDecrease"), {
                            .layout = {
                                .sizing = {.width = CLAY_SIZING_FIXED(50), .height = CLAY_SIZING_FIXED(50)},
                                .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
                            },
                            .backgroundColor = Clay_Hovered() ? COLOR_SECONDARY : COLOR_INPUT_BG,
                            .cornerRadius = CLAY_CORNER_RADIUS(8)
                            }) {
                            CLAY_TEXT(CLAY_STRING("<"), CLAY_TEXT_CONFIG({
                                .fontSize = 42,
                                .letterSpacing = 1.0f,
                                .textColor = COLOR_TEXT
                                }));

                            if (Clay_Hovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && appState.selectedDbIndex > 0) {
                                appState.selectedDbIndex--;

                                // Ajustar maxDocuments se necessário
                                int newTotalDocs = databases[appState.selectedDbIndex].totalDocs;
                                if (appState.maxDocuments > newTotalDocs) {
                                    appState.maxDocuments = newTotalDocs;
                                }
                                appState.numResults = 0;
                                snprintf(appState.statusMessage, sizeof(appState.statusMessage),
                                    "Banco alterado: %s (%d docs)",
                                    databases[appState.selectedDbIndex].displayName,
                                    newTotalDocs);
                            }
                        }

                        // Nome do banco atual
                        CLAY(CLAY_ID("DbValue"), {
                            .layout = {
                                .sizing = {.width = CLAY_SIZING_FIXED(350)},
                                .padding = CLAY_PADDING_ALL(12),
                                .childAlignment = {.x = CLAY_ALIGN_X_CENTER}
                            },
                            .backgroundColor = COLOR_PRIMARY,
                            .cornerRadius = CLAY_CORNER_RADIUS(8)
                            }) {
                            CLAY_TEXT(MakeClayString(databases[appState.selectedDbIndex].displayName), CLAY_TEXT_CONFIG({
                                .fontSize = 30,
                                .letterSpacing = 1.2f,
                                .textColor = COLOR_TEXT
                                }));
                        }

                        // Botão > (próximo)
                        CLAY(CLAY_ID("DbIncrease"), {
                            .layout = {
                                .sizing = {.width = CLAY_SIZING_FIXED(50), .height = CLAY_SIZING_FIXED(50)},
                                .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
                            },
                            .backgroundColor = Clay_Hovered() ? COLOR_SECONDARY : COLOR_INPUT_BG,
                            .cornerRadius = CLAY_CORNER_RADIUS(8)
                            }) {
                            CLAY_TEXT(CLAY_STRING(">"), CLAY_TEXT_CONFIG({
                                .fontSize = 42,
                                .letterSpacing = 1.0f,
                                .textColor = COLOR_TEXT
                                }));

                            if (Clay_Hovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && appState.selectedDbIndex < NUM_DATABASES - 1) {
                                appState.selectedDbIndex++;

                                // Ajustar maxDocuments se necessário
                                int newTotalDocs = databases[appState.selectedDbIndex].totalDocs;
                                if (appState.maxDocuments > newTotalDocs) {
                                    appState.maxDocuments = newTotalDocs;
                                }
                                appState.numResults = 0;
                                snprintf(appState.statusMessage, sizeof(appState.statusMessage),
                                    "Banco alterado: %s (%d docs)",
                                    databases[appState.selectedDbIndex].displayName,
                                    newTotalDocs);
                            }
                        }
                    }

                    CLAY_TEXT(CLAY_STRING("Use botoes para alternar entre bancos"), CLAY_TEXT_CONFIG({
                        .fontSize = 18,
                        .letterSpacing = 0.8f,
                        .textColor = COLOR_TEXT_DIM
                        }));
                }

                // Número de Threads
                CLAY(CLAY_ID("ThreadsConfig"), {
                    .layout = {
                        .sizing = {.width = CLAY_SIZING_GROW(0)},
                        .padding = CLAY_PADDING_ALL(20),
                        .childGap = 12,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                    },
                    .backgroundColor = COLOR_BG_CARD,
                    .cornerRadius = CLAY_CORNER_RADIUS(12)
                    }) {
                    CLAY_TEXT(CLAY_STRING("Numero de Threads"), CLAY_TEXT_CONFIG({
                        .fontSize = 27,
                        .letterSpacing = 1.2f,
                        .textColor = COLOR_TEXT
                        }));

                    // Container horizontal para botões + valor
                    CLAY(CLAY_ID("ThreadsControls"), {
                        .layout = {
                            .sizing = {.width = CLAY_SIZING_GROW(0)},
                            .childGap = 12,
                            .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
                        }
                        }) {

                        // Botão - (diminuir)
                        CLAY(CLAY_ID("ThreadsDecrease"), {
                            .layout = {
                                .sizing = {.width = CLAY_SIZING_FIXED(50), .height = CLAY_SIZING_FIXED(50)},
                                .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
                            },
                            .backgroundColor = Clay_Hovered() ? COLOR_SECONDARY : COLOR_INPUT_BG,
                            .cornerRadius = CLAY_CORNER_RADIUS(8)
                            }) {
                            CLAY_TEXT(CLAY_STRING("-"), CLAY_TEXT_CONFIG({
                                .fontSize = 42,
                                .letterSpacing = 1.0f,
                                .textColor = COLOR_TEXT
                                }));

                            if (Clay_Hovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && appState.numThreads > 1) {
                                appState.numThreads--;
                            }
                        }

                        // Valor atual
                        static char threadText[32];
                        snprintf(threadText, sizeof(threadText), "%d threads", appState.numThreads);
                        CLAY(CLAY_ID("ThreadsValue"), {
                            .layout = {
                                .sizing = {.width = CLAY_SIZING_FIXED(250)},
                                .padding = CLAY_PADDING_ALL(12),
                                .childAlignment = {.x = CLAY_ALIGN_X_CENTER}
                            },
                            .backgroundColor = COLOR_PRIMARY,
                            .cornerRadius = CLAY_CORNER_RADIUS(8)
                            }) {
                            CLAY_TEXT(MakeClayString(threadText), CLAY_TEXT_CONFIG({
                                .fontSize = 36,
                                .letterSpacing = 1.5f,
                                .textColor = COLOR_TEXT
                                }));
                        }

                        // Botão + (aumentar)
                        CLAY(CLAY_ID("ThreadsIncrease"), {
                            .layout = {
                                .sizing = {.width = CLAY_SIZING_FIXED(50), .height = CLAY_SIZING_FIXED(50)},
                                .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
                            },
                            .backgroundColor = Clay_Hovered() ? COLOR_SECONDARY : COLOR_INPUT_BG,
                            .cornerRadius = CLAY_CORNER_RADIUS(8)
                            }) {
                            CLAY_TEXT(CLAY_STRING("+"), CLAY_TEXT_CONFIG({
                                .fontSize = 42,
                                .letterSpacing = 1.0f,
                                .textColor = COLOR_TEXT
                                }));

                            if (Clay_Hovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && appState.numThreads < 16) {
                                appState.numThreads++;
                            }
                        }
                    }

                    CLAY_TEXT(CLAY_STRING("Use botoes ou setas ^ v para ajustar (1-16)"), CLAY_TEXT_CONFIG({
                        .fontSize = 18,
                        .letterSpacing = 0.8f,
                        .textColor = COLOR_TEXT_DIM
                        }));
                }
            }

            // ========== CONTROLE DE DOCUMENTOS ==========
            CLAY(CLAY_ID("DocsSection"), {
                .layout = {
                    .sizing = {.width = CLAY_SIZING_GROW(0)},
                    .padding = CLAY_PADDING_ALL(20),
                    .childGap = 12,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = COLOR_BG_CARD,
                .cornerRadius = CLAY_CORNER_RADIUS(12)
                }) {
                CLAY_TEXT(CLAY_STRING("Documentos"), CLAY_TEXT_CONFIG({
                    .fontSize = 30,
                    .letterSpacing = 1.2f,
                    .textColor = COLOR_TEXT
                    }));

                // Container dos controles de documentos
                CLAY(CLAY_ID("DocsControls"), {
                    .layout = {
                        .layoutDirection = CLAY_LEFT_TO_RIGHT,
                        .sizing = {.width = CLAY_SIZING_GROW(0)},
                        .childGap = 12,
                        .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
                    }
                    }) {

                    // Botão - (diminuir)
                    CLAY(CLAY_ID("DocsDecrease"), {
                        .layout = {
                            .sizing = {.width = CLAY_SIZING_FIXED(50), .height = CLAY_SIZING_FIXED(50)},
                            .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
                        },
                        .backgroundColor = Clay_Hovered() ? COLOR_SECONDARY : COLOR_INPUT_BG,
                        .cornerRadius = CLAY_CORNER_RADIUS(8)
                        }) {
                        CLAY_TEXT(CLAY_STRING("-"), CLAY_TEXT_CONFIG({
                            .fontSize = 42,
                            .letterSpacing = 1.0f,
                            .textColor = COLOR_TEXT
                            }));

                        if (Clay_Hovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                            // Diminuir em saltos: 1000, 5000, 10000, 25000, 50000, 97549
                            if (appState.maxDocuments > 50000) appState.maxDocuments = 50000;
                            else if (appState.maxDocuments > 25000) appState.maxDocuments = 25000;
                            else if (appState.maxDocuments > 10000) appState.maxDocuments = 10000;
                            else if (appState.maxDocuments > 5000) appState.maxDocuments = 5000;
                            else if (appState.maxDocuments > 1000) appState.maxDocuments = 1000;
                        }
                    }

                    // Valor atual
                    static char docsText[64];
                    int totalDocsInDb = databases[appState.selectedDbIndex].totalDocs;
                    if (appState.maxDocuments >= totalDocsInDb) {
                        snprintf(docsText, sizeof(docsText), "Todos (%d)", totalDocsInDb);
                    } else {
                        snprintf(docsText, sizeof(docsText), "%d docs", appState.maxDocuments);
                    }
                    CLAY(CLAY_ID("DocsValue"), {
                        .layout = {
                            .sizing = {.width = CLAY_SIZING_FIXED(280)},
                            .padding = CLAY_PADDING_ALL(12),
                            .childAlignment = {.x = CLAY_ALIGN_X_CENTER}
                        },
                        .backgroundColor = COLOR_SUCCESS,
                        .cornerRadius = CLAY_CORNER_RADIUS(8)
                        }) {
                        CLAY_TEXT(MakeClayString(docsText), CLAY_TEXT_CONFIG({
                            .fontSize = 36,
                            .letterSpacing = 1.5f,
                            .textColor = COLOR_TEXT
                            }));
                    }

                    // Botão + (aumentar)
                    CLAY(CLAY_ID("DocsIncrease"), {
                        .layout = {
                            .sizing = {.width = CLAY_SIZING_FIXED(50), .height = CLAY_SIZING_FIXED(50)},
                            .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
                        },
                        .backgroundColor = Clay_Hovered() ? COLOR_SECONDARY : COLOR_INPUT_BG,
                        .cornerRadius = CLAY_CORNER_RADIUS(8)
                        }) {
                        CLAY_TEXT(CLAY_STRING("+"), CLAY_TEXT_CONFIG({
                            .fontSize = 42,
                            .letterSpacing = 1.0f,
                            .textColor = COLOR_TEXT
                            }));

                        if (Clay_Hovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                            // Aumentar em saltos: 1000, 5000, 10000, 25000, 50000, todos
                            int totalDocsInDb = databases[appState.selectedDbIndex].totalDocs;
                            if (appState.maxDocuments < 1000) appState.maxDocuments = 1000;
                            else if (appState.maxDocuments < 5000) appState.maxDocuments = 5000;
                            else if (appState.maxDocuments < 10000) appState.maxDocuments = 10000;
                            else if (appState.maxDocuments < 25000) appState.maxDocuments = 25000;
                            else if (appState.maxDocuments < 50000) appState.maxDocuments = 50000;
                            else appState.maxDocuments = totalDocsInDb;
                        }
                    }
                }

                CLAY_TEXT(CLAY_STRING("Use botoes ou ALT + setas <- -> para: 1k, 5k, 10k, 25k, 50k, Todos"), CLAY_TEXT_CONFIG({
                    .fontSize = 18,
                    .letterSpacing = 0.8f,
                    .textColor = COLOR_TEXT_DIM
                    }));
            }

            // ========== CONTROLE DE TOP-K ==========
            CLAY(CLAY_ID("TopKSection"), {
                .layout = {
                    .sizing = {.width = CLAY_SIZING_GROW(0)},
                    .padding = CLAY_PADDING_ALL(20),
                    .childGap = 12,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = COLOR_BG_CARD,
                .cornerRadius = CLAY_CORNER_RADIUS(12)
                }) {
                CLAY_TEXT(CLAY_STRING("Top-K Resultados"), CLAY_TEXT_CONFIG({
                    .fontSize = 30,
                    .letterSpacing = 1.2f,
                    .textColor = COLOR_TEXT
                    }));

                // Container dos controles de top-k
                CLAY(CLAY_ID("TopKControls"), {
                    .layout = {
                        .layoutDirection = CLAY_LEFT_TO_RIGHT,
                        .sizing = {.width = CLAY_SIZING_GROW(0)},
                        .childGap = 12,
                        .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
                    }
                    }) {

                    // Botão - (diminuir)
                    CLAY(CLAY_ID("TopKDecrease"), {
                        .layout = {
                            .sizing = {.width = CLAY_SIZING_FIXED(50), .height = CLAY_SIZING_FIXED(50)},
                            .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
                        },
                        .backgroundColor = Clay_Hovered() ? COLOR_SECONDARY : COLOR_INPUT_BG,
                        .cornerRadius = CLAY_CORNER_RADIUS(8)
                        }) {
                        CLAY_TEXT(CLAY_STRING("-"), CLAY_TEXT_CONFIG({
                            .fontSize = 42,
                            .letterSpacing = 1.0f,
                            .textColor = COLOR_TEXT
                            }));

                        if (Clay_Hovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && appState.topK > 1) {
                            appState.topK--;
                        }
                    }

                    // Valor atual
                    static char topKText[32];
                    snprintf(topKText, sizeof(topKText), "Top %d", appState.topK);
                    CLAY(CLAY_ID("TopKValue"), {
                        .layout = {
                            .sizing = {.width = CLAY_SIZING_FIXED(280)},
                            .padding = CLAY_PADDING_ALL(12),
                            .childAlignment = {.x = CLAY_ALIGN_X_CENTER}
                        },
                        .backgroundColor = COLOR_PRIMARY,
                        .cornerRadius = CLAY_CORNER_RADIUS(8)
                        }) {
                        CLAY_TEXT(MakeClayString(topKText), CLAY_TEXT_CONFIG({
                            .fontSize = 36,
                            .letterSpacing = 1.5f,
                            .textColor = COLOR_TEXT
                            }));
                    }

                    // Botão + (aumentar)
                    CLAY(CLAY_ID("TopKIncrease"), {
                        .layout = {
                            .sizing = {.width = CLAY_SIZING_FIXED(50), .height = CLAY_SIZING_FIXED(50)},
                            .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
                        },
                        .backgroundColor = Clay_Hovered() ? COLOR_SECONDARY : COLOR_INPUT_BG,
                        .cornerRadius = CLAY_CORNER_RADIUS(8)
                        }) {
                        CLAY_TEXT(CLAY_STRING("+"), CLAY_TEXT_CONFIG({
                            .fontSize = 42,
                            .letterSpacing = 1.0f,
                            .textColor = COLOR_TEXT
                            }));

                        if (Clay_Hovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && appState.topK < 10) {
                            appState.topK++;
                        }
                    }
                }

                CLAY_TEXT(CLAY_STRING("Quantidade de resultados a exibir (1-10)"), CLAY_TEXT_CONFIG({
                    .fontSize = 18,
                    .letterSpacing = 0.8f,
                    .textColor = COLOR_TEXT_DIM
                    }));
            }

            // ========== LOADING / STATUS ==========
            CLAY(CLAY_ID("StatusSection"), {
                .layout = {
                    .sizing = {.width = CLAY_SIZING_GROW(0)},
                    .padding = CLAY_PADDING_ALL(20),
                    .childGap = 16,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = COLOR_BG_CARD,
                .cornerRadius = CLAY_CORNER_RADIUS(12),
                .border = {
                    .width = {0, 0, 0, 4, 0},
                    .color = appState.isProcessing ? COLOR_PRIMARY : COLOR_SUCCESS
                }
                }) {
                CLAY_TEXT(CLAY_STRING("Status do Sistema"), CLAY_TEXT_CONFIG({
                    .fontSize = 27,
                    .letterSpacing = 1.2f,
                    .textColor = COLOR_TEXT
                    }));

                // Status message com indicador de carregamento
                if (appState.isProcessing) {
                    static char loadingMsg[256];
                    static int dotCount = 0;
                    dotCount = (dotCount + 1) % 4;  // Animar pontos (0, 1, 2, 3)

                    snprintf(loadingMsg, sizeof(loadingMsg), "%s%.*s",
                        appState.statusMessage, dotCount, "...");

                    CLAY_TEXT(MakeClayString(loadingMsg), CLAY_TEXT_CONFIG({
                        .fontSize = 24,
                        .letterSpacing = 1.0f,
                        .textColor = COLOR_PRIMARY
                        }));
                } else {
                    CLAY_TEXT(MakeClayString(appState.statusMessage), CLAY_TEXT_CONFIG({
                        .fontSize = 24,
                        .letterSpacing = 1.0f,
                        .textColor = COLOR_SUCCESS
                        }));
                }

                // Barra de progresso (se processando)
                if (appState.isProcessing) {
                    CLAY(CLAY_ID("ProgressBar"), {
                        .layout = {
                            .sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(8)},
                        },
                        .backgroundColor = COLOR_BORDER,
                        .cornerRadius = CLAY_CORNER_RADIUS(4)
                        }) {
                    }

                    // Progresso (animado)
                    char progressText[32];
                    snprintf(progressText, sizeof(progressText), "Progresso: %.0f%%", appState.processingProgress * 100);
                    CLAY_TEXT(MakeClayString(progressText), CLAY_TEXT_CONFIG({
                        .fontSize = 21,
                        .letterSpacing = 0.8f,
                        .textColor = COLOR_TEXT_DIM
                        }));
                }
            }

            // ========== BOTÃO DE AÇÃO ==========
            CLAY(CLAY_ID("SearchButton"), {
                .layout = {
                    .sizing = {.width = CLAY_SIZING_GROW(0)},
                    .padding = {20, 20, 20, 20},
                    .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
                },
                .backgroundColor = Clay_Hovered() ? COLOR_PRIMARY_HOVER : COLOR_PRIMARY,
                .cornerRadius = CLAY_CORNER_RADIUS(12)
                }) {
                if (appState.isProcessing) {
                    CLAY_TEXT(CLAY_STRING("Processando..."), CLAY_TEXT_CONFIG({
                        .fontSize = 30,
                        .letterSpacing = 1.5f,
                        .textColor = COLOR_TEXT
                        }));
                } else {
                    CLAY_TEXT(CLAY_STRING("Buscar Documentos"), CLAY_TEXT_CONFIG({
                        .fontSize = 30,
                        .letterSpacing = 1.5f,
                        .textColor = COLOR_TEXT
                        }));
                }

                // Detectar clique APENAS neste botão
                if (Clay_Hovered() && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !appState.isProcessing) {
                    // Verificar se há query
                    if (strlen(appState.searchQuery) == 0) {
                        snprintf(appState.statusMessage, sizeof(appState.statusMessage),
                            "Digite uma consulta primeiro!");
                        appState.numResults = 0;
                    } else {
                        // Marcar como processando
                        appState.isProcessing = true;
                        DatabaseConfig *currentDb = &databases[appState.selectedDbIndex];
                        snprintf(appState.statusMessage, sizeof(appState.statusMessage),
                            "Buscando em %s com %d threads...",
                            currentDb->displayName, appState.numThreads);

                        // Medir tempo de busca
                        clock_t start_time = clock();

                        // BUSCA REAL TF-IDF
                        appState.numResults = perform_tfidf_search(
                            currentDb->path,
                            currentDb->table,
                            appState.searchQuery,
                            appState.numThreads,
                            appState.maxDocuments,
                            currentDb->totalDocs,
                            appState.topK,
                            appState.results
                        );

                        // Calcular tempo decorrido
                        clock_t end_time = clock();
                        appState.searchTime = ((double) (end_time - start_time)) / CLOCKS_PER_SEC;
                        appState.searchThreads = appState.numThreads;

                        // Finalizar processamento imediatamente
                        appState.isProcessing = false;
                        snprintf(appState.statusMessage, sizeof(appState.statusMessage),
                            "Busca concluida! %d documentos encontrados em %.3fs",
                            appState.numResults, appState.searchTime);
                    }
                }
            }

            // ========== RESULTADOS DA BUSCA ==========
            if (appState.numResults > 0) {
                CLAY(CLAY_ID("ResultsSection"), {
                    .layout = {
                        .sizing = {.width = CLAY_SIZING_GROW(0)},
                        .padding = CLAY_PADDING_ALL(12),
                        .childGap = 6,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM
                    },
                    .backgroundColor = COLOR_BG_CARD,
                    .cornerRadius = CLAY_CORNER_RADIUS(12)
                    }) {
                    // Título
                    CLAY_TEXT(CLAY_STRING("Resultados da Busca"), CLAY_TEXT_CONFIG({
                        .fontSize = 24,
                        .letterSpacing = 1.0f,
                        .textColor = COLOR_SUCCESS
                        }));

                    // Card único contendo toda a saída (cada linha separada)
                    // Sem altura fixa - deixa crescer naturalmente e usa scroll da página principal
                    CLAY(CLAY_ID("TerminalCard"), {
                        .layout = {
                            .sizing = {
                                .width = CLAY_SIZING_GROW(0)
                            },
                            .padding = CLAY_PADDING_ALL(12),
                            .childGap = 2,
                            .layoutDirection = CLAY_TOP_TO_BOTTOM
                        },
                        .backgroundColor = COLOR_INPUT_BG,
                        .cornerRadius = CLAY_CORNER_RADIUS(8)
                        }) {

                        // Preparar strings estáticas para cada linha de resultado
                        static char resultLines[20][512];  // 2 linhas por resultado

                        for (int i = 0; i < appState.numResults && i < appState.topK; i++) {
                            // Linha 1: Número, DocID e Score
                            snprintf(resultLines[i * 2], sizeof(resultLines[i * 2]),
                                "[%d] DocID: %ld | Score: %.4f",
                                i + 1,
                                appState.results[i].docId,
                                appState.results[i].score);

                            CLAY_TEXT(MakeClayString(resultLines[i * 2]), CLAY_TEXT_CONFIG({
                                .fontSize = 20,
                                .letterSpacing = 0.6f,
                                .textColor = COLOR_PRIMARY
                                }));

                            // Linha 2: Preview do documento (com indentação)
                            if (appState.results[i].title[0] != '\0') {
                                snprintf(resultLines[i * 2 + 1], sizeof(resultLines[i * 2 + 1]),
                                    "    %s", appState.results[i].title);
                            } else {
                                snprintf(resultLines[i * 2 + 1], sizeof(resultLines[i * 2 + 1]),
                                    "    (no preview available)");
                            }

                            CLAY_TEXT(MakeClayString(resultLines[i * 2 + 1]), CLAY_TEXT_CONFIG({
                                .fontSize = 18,
                                .letterSpacing = 0.5f,
                                .textColor = COLOR_TEXT_DIM
                                }));
                        }
                    }

                    // Informações de performance
                    static char perfInfo[128];
                    snprintf(perfInfo, sizeof(perfInfo),
                        "Busca realizada em %.3fs usando %d thread%s",
                        appState.searchTime, appState.searchThreads,
                        appState.searchThreads > 1 ? "s" : "");

                    CLAY_TEXT(MakeClayString(perfInfo), CLAY_TEXT_CONFIG({
                        .fontSize = 17,
                        .letterSpacing = 0.5f,
                        .textColor = COLOR_SUCCESS
                        }));
                }
            }

            // ========== RODAPÉ ==========
            CLAY(CLAY_ID("Footer"), {
                .layout = {
                    .sizing = {.width = CLAY_SIZING_GROW(0)},
                    .padding = CLAY_PADDING_ALL(16),
                    .childAlignment = {.x = CLAY_ALIGN_X_CENTER}
                },
                .backgroundColor = COLOR_BG_CARD,
                .cornerRadius = CLAY_CORNER_RADIUS(8)
                }) {
                CLAY_TEXT(CLAY_STRING("ESC: Sair | Roda: Scroll | ^v: Threads | Use botoes para DB e Docs"), CLAY_TEXT_CONFIG({
                    .fontSize = 18,
                    .letterSpacing = 0.8f,
                    .textColor = COLOR_TEXT_DIM
                    }));
            }
        } // Fecha MainContainer
    } // Fecha ScrollContainer
}

int main(void) {
    // Configurações da janela
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_MAXIMIZED);
    InitWindow(1920, 1080, "TF-IDF Document Search - Clay + Raylib");
    SetWindowMinSize(1280, 800);
    SetTargetFPS(60);

    // Carregar fonte pixelada (usar a fonte padrão do Raylib como base)
    // Para fonte pixel art, podemos aumentar o tamanho base e reduzir espaçamento
    pixelFont = GetFontDefault();

    // ========== INICIALIZAR BACKEND TF-IDF ==========
    printf("Carregando stopwords...\n");
    load_stopwords("assets/stopwords.txt");
    if (global_stopwords == NULL) {
        printf("[AVISO] Stopwords não carregadas. Buscas podem não funcionar corretamente.\n");
    } else {
        printf("Stopwords carregadas: %ld termos\n", global_stopwords->size);
    }

    // Inicializar estado da aplicação
    strcpy(appState.searchQuery, "");
    appState.selectedDbIndex = 0;
    appState.numThreads = 4;
    appState.isProcessing = false;
    appState.processingProgress = 0.0f;
    strcpy(appState.statusMessage, "Sistema pronto para buscar");
    appState.inputActive = false;
    appState.cursorPosition = 0;
    appState.numResults = 0;

    // Inicializar Clay
    uint64_t totalMemorySize = Clay_MinMemorySize();
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(
        totalMemorySize,
        malloc(totalMemorySize)
    );

    Clay_Initialize(
        arena,
        (Clay_Dimensions) {
        GetScreenWidth(), GetScreenHeight()
    },
        (Clay_ErrorHandler) {
        .errorHandlerFunction = HandleClayErrors,
            .userData = NULL
    }
    );

    Clay_SetMeasureTextFunction(MeasureTextForClay, NULL);

    // Loop principal
    while (!WindowShouldClose()) {
        // ========== PROCESSAMENTO DE SCROLL ==========
        // Scroll da página principal (sempre ativo)
        float wheelMove = GetMouseWheelMove();
        if (wheelMove != 0) {
            appState.scrollOffset += wheelMove * 40.0f;  // 40px por scroll (invertido)

            // Limitar scroll (não pode rolar para cima além do topo)
            if (appState.scrollOffset > 0) {
                appState.scrollOffset = 0;
            }
        }        // Scroll para resultados (desabilitado por enquanto, usando scroll principal)
        appState.resultsScrollOffset = 0;            // ========== PROCESSAMENTO DE INPUT DE TEXTO ==========
        if (appState.inputActive) {
            // Capturar caracteres digitados
            int key = GetCharPressed();
            while (key > 0) {
                if ((key >= 32) && (key <= 125) && (strlen(appState.searchQuery) < 255)) {
                    int len = strlen(appState.searchQuery);
                    appState.searchQuery[len] = (char) key;
                    appState.searchQuery[len + 1] = '\0';
                }
                key = GetCharPressed();
            }

            // Backspace para apagar
            if (IsKeyPressed(KEY_BACKSPACE)) {
                int len = strlen(appState.searchQuery);
                if (len > 0) {
                    appState.searchQuery[len - 1] = '\0';
                }
            }

            // Enter para confirmar e desativar input
            if (IsKeyPressed(KEY_ENTER)) {
                appState.inputActive = false;
            }

            // ESC para cancelar input
            if (IsKeyPressed(KEY_ESCAPE)) {
                appState.inputActive = false;
            }
        }

        // Controles de teclado (apenas quando input não está ativo)
        if (!appState.inputActive) {
            // CTRL + setas: Mudar banco de dados
            if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
                int oldDbIndex = appState.selectedDbIndex;

                if (IsKeyPressed(KEY_LEFT) && appState.selectedDbIndex > 0) {
                    appState.selectedDbIndex--;
                }
                if (IsKeyPressed(KEY_RIGHT) && appState.selectedDbIndex < NUM_DATABASES - 1) {
                    appState.selectedDbIndex++;
                }

                // Se mudou de banco, ajustar maxDocuments
                if (oldDbIndex != appState.selectedDbIndex) {
                    int newTotalDocs = databases[appState.selectedDbIndex].totalDocs;
                    // Ajustar maxDocuments se exceder o novo total
                    if (appState.maxDocuments > newTotalDocs) {
                        appState.maxDocuments = newTotalDocs;
                    }
                    // Limpar resultados anteriores
                    appState.numResults = 0;
                    snprintf(appState.statusMessage, sizeof(appState.statusMessage),
                        "Banco alterado: %s (%d docs)",
                        databases[appState.selectedDbIndex].displayName,
                        newTotalDocs);
                }
            }
            // ALT + setas: Ajustar quantidade de documentos
            else if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) {
                int totalDocsInDb = databases[appState.selectedDbIndex].totalDocs;

                if (IsKeyPressed(KEY_LEFT)) {
                    // Diminuir em saltos
                    if (appState.maxDocuments > 50000) appState.maxDocuments = 50000;
                    else if (appState.maxDocuments > 25000) appState.maxDocuments = 25000;
                    else if (appState.maxDocuments > 10000) appState.maxDocuments = 10000;
                    else if (appState.maxDocuments > 5000) appState.maxDocuments = 5000;
                    else if (appState.maxDocuments > 1000) appState.maxDocuments = 1000;
                }
                if (IsKeyPressed(KEY_RIGHT)) {
                    // Aumentar em saltos
                    if (appState.maxDocuments < 1000) appState.maxDocuments = 1000;
                    else if (appState.maxDocuments < 5000) appState.maxDocuments = 5000;
                    else if (appState.maxDocuments < 10000) appState.maxDocuments = 10000;
                    else if (appState.maxDocuments < 25000) appState.maxDocuments = 25000;
                    else if (appState.maxDocuments < 50000) appState.maxDocuments = 50000;
                    else appState.maxDocuments = totalDocsInDb;
                }
            }

            // Setas UP/DOWN: Threads (sem modificador)
            if (IsKeyPressed(KEY_UP) && appState.numThreads < 16) {
                appState.numThreads++;
            }
            if (IsKeyPressed(KEY_DOWN) && appState.numThreads > 1) {
                appState.numThreads--;
            }
        }

        // Atualizar dimensões se a janela foi redimensionada
        Clay_SetLayoutDimensions((Clay_Dimensions) { GetScreenWidth(), GetScreenHeight() });

        // Atualizar posição do mouse
        Vector2 mousePos = GetMousePosition();

        // Atualizar dimensões do Clay se janela foi redimensionada
        Clay_SetLayoutDimensions((Clay_Dimensions) {
            GetScreenWidth(),
                GetScreenHeight()
        });

        Clay_SetPointerState(
            (Clay_Vector2) {
            mousePos.x, mousePos.y  // Clay gerencia scroll internamente
        },
            IsMouseButtonDown(MOUSE_LEFT_BUTTON)
        );

        // Começar layout
        Clay_BeginLayout();

        // Criar UI
        CreateUI();

        // Finalizar layout e obter comandos de renderização
        Clay_RenderCommandArray renderCommands = Clay_EndLayout();

        // Renderizar
        BeginDrawing();
        ClearBackground((Color) { 43, 41, 51, 255 });

        // Processar comandos de renderização (Clay gerencia scroll via childOffset)
        for (int i = 0; i < renderCommands.length; i++) {
            Clay_RenderCommand *cmd = &renderCommands.internalArray[i];

            switch (cmd->commandType) {
                case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
                    RenderRectangle(cmd);
                    break;
                case CLAY_RENDER_COMMAND_TYPE_TEXT:
                    RenderText(cmd);
                    break;
                case CLAY_RENDER_COMMAND_TYPE_BORDER:
                    RenderBorder(cmd);
                    break;
                case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
                    BeginScissorMode(
                        cmd->boundingBox.x,
                        cmd->boundingBox.y,
                        cmd->boundingBox.width,
                        cmd->boundingBox.height
                    );
                    break;
                case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
                    EndScissorMode();
                    break;
                default:
                    break;
            }
        }

        EndDrawing();
    }

    // Liberar recursos do backend
    printf("Liberando recursos...\n");
    free_stopwords();

    CloseWindow();
    return 0;
}
