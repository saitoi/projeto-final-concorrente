/**
 * @file ui_clay_menu.c
 * @brief Interface visual com menu interativo para o sistema TF-IDF
 * @details Menu completo com opções de busca, testes e configurações
 */

#define CLAY_IMPLEMENTATION
#include "clay.h"

#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

 // ============================================================================
 // PALETA DE CORES MODERNA
 // ============================================================================
const Clay_Color COLOR_BG_DARK = { 23, 23, 28, 255 };           // Fundo escuro elegante
const Clay_Color COLOR_BG_CARD = { 35, 35, 45, 255 };           // Cards/Containers
const Clay_Color COLOR_PRIMARY = { 139, 92, 246, 255 };         // Roxo vibrante
const Clay_Color COLOR_PRIMARY_HOVER = { 124, 58, 237, 255 };   // Roxo escuro (hover)
const Clay_Color COLOR_SECONDARY = { 59, 130, 246, 255 };       // Azul
const Clay_Color COLOR_SUCCESS = { 34, 197, 94, 255 };          // Verde
const Clay_Color COLOR_WARNING = { 251, 191, 36, 255 };         // Amarelo
const Clay_Color COLOR_DANGER = { 239, 68, 68, 255 };           // Vermelho
const Clay_Color COLOR_TEXT = { 248, 250, 252, 255 };           // Branco suave
const Clay_Color COLOR_TEXT_DIM = { 148, 163, 184, 255 };       // Cinza claro
const Clay_Color COLOR_BORDER = { 51, 65, 85, 255 };            // Borda sutil

// ============================================================================
// ESTADO DA APLICAÇÃO
// ============================================================================
typedef enum {
    VIEW_HOME,
    VIEW_SEARCH,
    VIEW_TESTS,
    VIEW_SETTINGS,
    VIEW_ABOUT
} ViewType;

typedef struct {
    ViewType currentView;
    bool isSearching;
    bool testsRunning;
    int selectedTest;
    char searchQuery[256];
    int numThreads;
    int numDocuments;
    bool verboseMode;

    // Estatísticas
    int documentsProcessed;
    int totalDocuments;
    float processingTime;
    int resultsFound;
} AppState;

AppState appState = {
    .currentView = VIEW_HOME,
    .isSearching = false,
    .testsRunning = false,
    .selectedTest = -1,
    .searchQuery = "",
    .numThreads = 4,
    .numDocuments = 100,
    .verboseMode = false,
    .documentsProcessed = 0,
    .totalDocuments = 1000,
    .processingTime = 0.0f,
    .resultsFound = 0
};

// ============================================================================
// FUNÇÕES AUXILIARES
// ============================================================================

Clay_Dimensions MeasureTextForClay(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    (void) userData;

    char *tempString = (char *) malloc(text.length + 1);
    memcpy(tempString, text.chars, text.length);
    tempString[text.length] = '\0';

    Vector2 measured = MeasureTextEx(
        GetFontDefault(),
        tempString,
        config->fontSize,
        config->letterSpacing
    );

    free(tempString);

    return (Clay_Dimensions) {
        .width = measured.x,
            .height = measured.y
    };
}

void HandleClayErrors(Clay_ErrorData errorData) {
    printf("[CLAY ERROR] %s\n", errorData.errorText.chars);
}

// ============================================================================
// RENDERIZAÇÃO
// ============================================================================

void RenderRectangle(Clay_RenderCommand *cmd) {
    Clay_RectangleRenderData *data = &cmd->renderData.rectangle;
    Clay_BoundingBox bbox = cmd->boundingBox;

    Color color = { data->backgroundColor.r, data->backgroundColor.g,
                   data->backgroundColor.b, data->backgroundColor.a };

    if (data->cornerRadius.topLeft > 0) {
        DrawRectangleRounded(
            (Rectangle) {
            bbox.x, bbox.y, bbox.width, bbox.height
        },
            data->cornerRadius.topLeft / (bbox.width < bbox.height ? bbox.width : bbox.height),
            16,
            color
        );
    } else {
        DrawRectangle(bbox.x, bbox.y, bbox.width, bbox.height, color);
    }
}

void RenderText(Clay_RenderCommand *cmd) {
    Clay_TextRenderData *data = &cmd->renderData.text;
    Clay_BoundingBox bbox = cmd->boundingBox;

    Color color = { data->textColor.r, data->textColor.g,
                   data->textColor.b, data->textColor.a };

    char *tempString = (char *) malloc(data->stringContents.length + 1);
    memcpy(tempString, data->stringContents.chars, data->stringContents.length);
    tempString[data->stringContents.length] = '\0';

    DrawTextEx(
        GetFontDefault(),
        tempString,
        (Vector2) {
        bbox.x, bbox.y
    },
        data->fontSize,
        data->letterSpacing,
        color
    );

    free(tempString);
}

void RenderBorder(Clay_RenderCommand *cmd) {
    Clay_BorderRenderData *data = &cmd->renderData.border;
    Clay_BoundingBox bbox = cmd->boundingBox;

    Color color = { data->color.r, data->color.g, data->color.b, data->color.a };

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

// ============================================================================
// COMPONENTES UI
// ============================================================================

void MenuButton(const char *icon, const char *label, ViewType view, bool isActive) {
    Clay_Color bgColor = isActive ? COLOR_PRIMARY : COLOR_BG_CARD;
    Clay_Color hoverColor = isActive ? COLOR_PRIMARY_HOVER : COLOR_PRIMARY;

    bool isHovered = Clay_Hovered();
    Clay_Color currentColor = isHovered ? hoverColor : bgColor;

    CLAY(CLAY_IDI("MenuBtn", view), {
        .layout = {
            .sizing = {.width = CLAY_SIZING_GROW(0)},
            .padding = CLAY_PADDING_ALL(16),
            .childGap = 12,
            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}
        },
        .backgroundColor = currentColor,
        .cornerRadius = CLAY_CORNER_RADIUS(12)
        }) {
        CLAY_TEXT(CLAY_STRING(icon), CLAY_TEXT_CONFIG({
            .fontSize = 24,
            .textColor = COLOR_TEXT
            }));
        CLAY_TEXT(CLAY_STRING(label), CLAY_TEXT_CONFIG({
            .fontSize = 16,
            .textColor = COLOR_TEXT
            }));
    }

    if (isHovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        appState.currentView = view;
    }
}

void StatCard(const char *label, const char *value, Clay_Color accentColor) {
    CLAY(CLAY_AUTO_ID(), {
        .layout = {
            .sizing = {.width = CLAY_SIZING_GROW(0)},
            .padding = CLAY_PADDING_ALL(20),
            .childGap = 8,
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = COLOR_BG_CARD,
        .cornerRadius = CLAY_CORNER_RADIUS(12),
        .border = {
            .width = {0, 0, 4, 0, 0},
            .color = accentColor
        }
        }) {
        CLAY_TEXT(CLAY_STRING(label), CLAY_TEXT_CONFIG({
            .fontSize = 14,
            .textColor = COLOR_TEXT_DIM
            }));
        CLAY_TEXT(CLAY_STRING(value), CLAY_TEXT_CONFIG({
            .fontSize = 28,
            .textColor = COLOR_TEXT
            }));
    }
}

void TestOption(int testId, const char *name, const char *description) {
    bool isSelected = appState.selectedTest == testId;
    bool isHovered = Clay_Hovered();

    Clay_Color bgColor = isSelected ? COLOR_PRIMARY : COLOR_BG_CARD;
    if (!isSelected && isHovered) {
        bgColor = COLOR_BORDER;
    }

    CLAY(CLAY_IDI("Test", testId), {
        .layout = {
            .sizing = {.width = CLAY_SIZING_GROW(0)},
            .padding = CLAY_PADDING_ALL(16),
            .childGap = 8,
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        },
        .backgroundColor = bgColor,
        .cornerRadius = CLAY_CORNER_RADIUS(10)
        }) {
        CLAY_TEXT(CLAY_STRING(name), CLAY_TEXT_CONFIG({
            .fontSize = 18,
            .textColor = COLOR_TEXT
            }));
        CLAY_TEXT(CLAY_STRING(description), CLAY_TEXT_CONFIG({
            .fontSize = 14,
            .textColor = COLOR_TEXT_DIM
            }));
    }

    if (isHovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        appState.selectedTest = testId;
    }
}

void ActionButton(const char *label, Clay_Color color, void (*onClick)(void)) {
    bool isHovered = Clay_Hovered();

    CLAY(CLAY_AUTO_ID(), {
        .layout = {
            .padding = {16, 32, 16, 32},
            .childAlignment = {.x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER}
        },
        .backgroundColor = isHovered ? COLOR_PRIMARY_HOVER : color,
        .cornerRadius = CLAY_CORNER_RADIUS(10)
        }) {
        CLAY_TEXT(CLAY_STRING(label), CLAY_TEXT_CONFIG({
            .fontSize = 16,
            .textColor = COLOR_TEXT
            }));
    }

    if (isHovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && onClick) {
        onClick();
    }
}

// ============================================================================
// VIEWS
// ============================================================================

void RenderHomeView() {
    CLAY(CLAY_ID("HomeContent"), {
        .layout = {
            .sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
            .padding = CLAY_PADDING_ALL(32),
            .childGap = 24,
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        }
        }) {
        // Header
        CLAY(CLAY_ID("HomeHeader"), {
            .layout = {
                .sizing = {.width = CLAY_SIZING_GROW(0)},
                .childGap = 12,
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childAlignment = {.x = CLAY_ALIGN_X_CENTER}
            }
            }) {
            CLAY_TEXT(CLAY_STRING("🔍 TF-IDF Search System"), CLAY_TEXT_CONFIG({
                .fontSize = 42,
                .textColor = COLOR_TEXT
                }));
            CLAY_TEXT(CLAY_STRING("Sistema de Busca com Processamento Paralelo"), CLAY_TEXT_CONFIG({
                .fontSize = 18,
                .textColor = COLOR_TEXT_DIM
                }));
        }

        // Estatísticas
        CLAY(CLAY_ID("StatsRow"), {
            .layout = {
                .sizing = {.width = CLAY_SIZING_GROW(0)},
                .childGap = 16
            }
            }) {
            StatCard("Documentos", "1,000", COLOR_PRIMARY);
            StatCard("Threads", "4", COLOR_SECONDARY);
            StatCard("Processados", "0", COLOR_SUCCESS);
            StatCard("Resultados", "0", COLOR_WARNING);
        }

        // Descrição
        CLAY(CLAY_ID("Description"), {
            .layout = {
                .sizing = {.width = CLAY_SIZING_GROW(0)},
                .padding = CLAY_PADDING_ALL(24),
                .childGap = 12,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            },
            .backgroundColor = COLOR_BG_CARD,
            .cornerRadius = CLAY_CORNER_RADIUS(12)
            }) {
            CLAY_TEXT(CLAY_STRING("💡 Bem-vindo!"), CLAY_TEXT_CONFIG({
                .fontSize = 20,
                .textColor = COLOR_TEXT
                }));
            CLAY_TEXT(CLAY_STRING("Este sistema utiliza TF-IDF para ranquear documentos por relevância."), CLAY_TEXT_CONFIG({
                .fontSize = 15,
                .textColor = COLOR_TEXT_DIM
                }));
            CLAY_TEXT(CLAY_STRING("Use o menu lateral para navegar entre as opções."), CLAY_TEXT_CONFIG({
                .fontSize = 15,
                .textColor = COLOR_TEXT_DIM
                }));
        }
    }
}

void RenderSearchView() {
    CLAY(CLAY_ID("SearchContent"), {
        .layout = {
            .sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
            .padding = CLAY_PADDING_ALL(32),
            .childGap = 24,
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        }
        }) {
        CLAY_TEXT(CLAY_STRING("🔎 Buscar Documentos"), CLAY_TEXT_CONFIG({
            .fontSize = 32,
            .textColor = COLOR_TEXT
            }));

        // Campo de busca (placeholder)
        CLAY(CLAY_ID("SearchBox"), {
            .layout = {
                .sizing = {.width = CLAY_SIZING_GROW(0)},
                .padding = CLAY_PADDING_ALL(20)
            },
            .backgroundColor = COLOR_BG_CARD,
            .cornerRadius = CLAY_CORNER_RADIUS(12)
            }) {
            CLAY_TEXT(CLAY_STRING("Digite sua consulta aqui..."), CLAY_TEXT_CONFIG({
                .fontSize = 16,
                .textColor = COLOR_TEXT_DIM
                }));
        }

        // Botão de busca
        ActionButton("🚀 Buscar", COLOR_PRIMARY, NULL);

        // Resultados
        CLAY(CLAY_ID("Results"), {
            .layout = {
                .sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
                .padding = CLAY_PADDING_ALL(20),
                .childGap = 12,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            },
            .backgroundColor = COLOR_BG_CARD,
            .cornerRadius = CLAY_CORNER_RADIUS(12)
            }) {
            CLAY_TEXT(CLAY_STRING("📋 Resultados aparecerão aqui"), CLAY_TEXT_CONFIG({
                .fontSize = 16,
                .textColor = COLOR_TEXT_DIM
                }));
        }
    }
}

void RenderTestsView() {
    CLAY(CLAY_ID("TestsContent"), {
        .layout = {
            .sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
            .padding = CLAY_PADDING_ALL(32),
            .childGap = 24,
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        }
        }) {
        CLAY_TEXT(CLAY_STRING("🧪 Testes de Corretude"), CLAY_TEXT_CONFIG({
            .fontSize = 32,
            .textColor = COLOR_TEXT
            }));

        // Lista de testes
        CLAY(CLAY_ID("TestsList"), {
            .layout = {
                .sizing = {.width = CLAY_SIZING_GROW(0)},
                .childGap = 12,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            }
            }) {
            TestOption(0, "📊 Test Table 0", "Marine sea species - 10 documentos");
            TestOption(1, "📊 Test Table 1", "Technology keywords - 15 documentos");
            TestOption(2, "📊 Test Table 2", "Medical terms - 20 documentos");
            TestOption(3, "📊 Test Table 3", "Legal documents - 25 documentos");
            TestOption(4, "📚 Shakespeare Performance", "Textos completos de Shakespeare");
            TestOption(5, "🌐 Book Corpus", "Grande corpus de livros");
        }

        // Botões de ação
        CLAY(CLAY_ID("TestActions"), {
            .layout = {
                .childGap = 16
            }
            }) {
            ActionButton("▶️  Executar Teste", COLOR_SUCCESS, NULL);
            ActionButton("🔄 Executar Todos", COLOR_SECONDARY, NULL);
        }
    }
}

void RenderSettingsView() {
    CLAY(CLAY_ID("SettingsContent"), {
        .layout = {
            .sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
            .padding = CLAY_PADDING_ALL(32),
            .childGap = 24,
            .layoutDirection = CLAY_TOP_TO_BOTTOM
        }
        }) {
        CLAY_TEXT(CLAY_STRING("⚙️  Configurações"), CLAY_TEXT_CONFIG({
            .fontSize = 32,
            .textColor = COLOR_TEXT
            }));

        // Configurações
        CLAY(CLAY_ID("Settings"), {
            .layout = {
                .sizing = {.width = CLAY_SIZING_GROW(0)},
                .childGap = 16,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            }
            }) {
            CLAY(CLAY_AUTO_ID(), {
                .layout = {
                    .sizing = {.width = CLAY_SIZING_GROW(0)},
                    .padding = CLAY_PADDING_ALL(20),
                    .childGap = 12,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = COLOR_BG_CARD,
                .cornerRadius = CLAY_CORNER_RADIUS(12)
                }) {
                CLAY_TEXT(CLAY_STRING("🧵 Número de Threads"), CLAY_TEXT_CONFIG({
                    .fontSize = 18,
                    .textColor = COLOR_TEXT
                    }));
                CLAY_TEXT(CLAY_STRING("4 threads (1-16)"), CLAY_TEXT_CONFIG({
                    .fontSize = 15,
                    .textColor = COLOR_TEXT_DIM
                    }));
            }

            CLAY(CLAY_AUTO_ID(), {
                .layout = {
                    .sizing = {.width = CLAY_SIZING_GROW(0)},
                    .padding = CLAY_PADDING_ALL(20),
                    .childGap = 12,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = COLOR_BG_CARD,
                .cornerRadius = CLAY_CORNER_RADIUS(12)
                }) {
                CLAY_TEXT(CLAY_STRING("📄 Documentos a Processar"), CLAY_TEXT_CONFIG({
                    .fontSize = 18,
                    .textColor = COLOR_TEXT
                    }));
                CLAY_TEXT(CLAY_STRING("100 documentos"), CLAY_TEXT_CONFIG({
                    .fontSize = 15,
                    .textColor = COLOR_TEXT_DIM
                    }));
            }

            CLAY(CLAY_AUTO_ID(), {
                .layout = {
                    .sizing = {.width = CLAY_SIZING_GROW(0)},
                    .padding = CLAY_PADDING_ALL(20),
                    .childGap = 12,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM
                },
                .backgroundColor = COLOR_BG_CARD,
                .cornerRadius = CLAY_CORNER_RADIUS(12)
                }) {
                CLAY_TEXT(CLAY_STRING("📝 Modo Verbose"), CLAY_TEXT_CONFIG({
                    .fontSize = 18,
                    .textColor = COLOR_TEXT
                    }));
                CLAY_TEXT(CLAY_STRING("Desativado"), CLAY_TEXT_CONFIG({
                    .fontSize = 15,
                    .textColor = COLOR_TEXT_DIM
                    }));
            }
        }
    }
}

void RenderAboutView() {
    CLAY(CLAY_ID("AboutContent"), {
        .layout = {
            .sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
            .padding = CLAY_PADDING_ALL(32),
            .childGap = 24,
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .childAlignment = {.x = CLAY_ALIGN_X_CENTER}
        }
        }) {
        CLAY_TEXT(CLAY_STRING("ℹ️  Sobre o Projeto"), CLAY_TEXT_CONFIG({
            .fontSize = 32,
            .textColor = COLOR_TEXT
            }));

        CLAY(CLAY_ID("AboutCard"), {
            .layout = {
                .sizing = {.width = CLAY_SIZING_FIXED(600)},
                .padding = CLAY_PADDING_ALL(32),
                .childGap = 16,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            },
            .backgroundColor = COLOR_BG_CARD,
            .cornerRadius = CLAY_CORNER_RADIUS(16)
            }) {
            CLAY_TEXT(CLAY_STRING("TF-IDF Search System v1.0"), CLAY_TEXT_CONFIG({
                .fontSize = 24,
                .textColor = COLOR_PRIMARY
                }));

            CLAY_TEXT(CLAY_STRING("Sistema de busca de documentos utilizando TF-IDF"), CLAY_TEXT_CONFIG({
                .fontSize = 16,
                .textColor = COLOR_TEXT
                }));
            CLAY_TEXT(CLAY_STRING("com processamento paralelo usando pthreads."), CLAY_TEXT_CONFIG({
                .fontSize = 16,
                .textColor = COLOR_TEXT
                }));

            CLAY_TEXT(CLAY_STRING(""), CLAY_TEXT_CONFIG({ .fontSize = 8, .textColor = COLOR_BG_DARK }));

            CLAY_TEXT(CLAY_STRING("🛠️  Tecnologias:"), CLAY_TEXT_CONFIG({
                .fontSize = 18,
                .textColor = COLOR_TEXT
                }));
            CLAY_TEXT(CLAY_STRING("• C com pthreads"), CLAY_TEXT_CONFIG({
                .fontSize = 15,
                .textColor = COLOR_TEXT_DIM
                }));
            CLAY_TEXT(CLAY_STRING("• SQLite para armazenamento"), CLAY_TEXT_CONFIG({
                .fontSize = 15,
                .textColor = COLOR_TEXT_DIM
                }));
            CLAY_TEXT(CLAY_STRING("• Snowball Stemmer (NLP)"), CLAY_TEXT_CONFIG({
                .fontSize = 15,
                .textColor = COLOR_TEXT_DIM
                }));
            CLAY_TEXT(CLAY_STRING("• Clay UI + Raylib"), CLAY_TEXT_CONFIG({
                .fontSize = 15,
                .textColor = COLOR_TEXT_DIM
                }));
        }
    }
}

// ============================================================================
// UI PRINCIPAL
// ============================================================================

void CreateUI(void) {
    CLAY(CLAY_ID("Root"), {
        .layout = {
            .sizing = {.width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0)},
            .childGap = 0
        },
        .backgroundColor = COLOR_BG_DARK
        }) {
        // Sidebar
        CLAY(CLAY_ID("Sidebar"), {
            .layout = {
                .sizing = {.width = CLAY_SIZING_FIXED(280), .height = CLAY_SIZING_GROW(0)},
                .padding = CLAY_PADDING_ALL(20),
                .childGap = 12,
                .layoutDirection = CLAY_TOP_TO_BOTTOM
            },
            .backgroundColor = COLOR_BG_CARD
            }) {
            // Logo
            CLAY(CLAY_ID("Logo"), {
                .layout = {
                    .sizing = {.width = CLAY_SIZING_GROW(0)},
                    .padding = CLAY_PADDING_ALL(16),
                    .childAlignment = {.x = CLAY_ALIGN_X_CENTER}
                },
                .backgroundColor = COLOR_PRIMARY,
                .cornerRadius = CLAY_CORNER_RADIUS(12)
                }) {
                CLAY_TEXT(CLAY_STRING("TF-IDF"), CLAY_TEXT_CONFIG({
                    .fontSize = 28,
                    .textColor = COLOR_TEXT
                    }));
            }

            // Menu items
            MenuButton("🏠", "Início", VIEW_HOME, appState.currentView == VIEW_HOME);
            MenuButton("🔍", "Buscar", VIEW_SEARCH, appState.currentView == VIEW_SEARCH);
            MenuButton("🧪", "Testes", VIEW_TESTS, appState.currentView == VIEW_TESTS);
            MenuButton("⚙️ ", "Configurações", VIEW_SETTINGS, appState.currentView == VIEW_SETTINGS);
            MenuButton("ℹ️ ", "Sobre", VIEW_ABOUT, appState.currentView == VIEW_ABOUT);
        }

        // Main content
        switch (appState.currentView) {
            case VIEW_HOME:
                RenderHomeView();
                break;
            case VIEW_SEARCH:
                RenderSearchView();
                break;
            case VIEW_TESTS:
                RenderTestsView();
                break;
            case VIEW_SETTINGS:
                RenderSettingsView();
                break;
            case VIEW_ABOUT:
                RenderAboutView();
                break;
        }
    }
}

// ============================================================================
// MAIN
// ============================================================================

int main(void) {
    const int screenWidth = 1400;
    const int screenHeight = 800;

    InitWindow(screenWidth, screenHeight, "TF-IDF UI - Sistema de Busca");
    SetTargetFPS(60);

    // Inicializar Clay
    uint64_t totalMemorySize = Clay_MinMemorySize();
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(
        totalMemorySize,
        malloc(totalMemorySize)
    );

    Clay_Initialize(
        arena,
        (Clay_Dimensions) {
        screenWidth, screenHeight
    },
        (Clay_ErrorHandler) {        
.errorHandlerFunction = HandleClayErrors, .userData = NULL    
}
    );

    Clay_SetMeasureTextFunction(MeasureTextForClay, NULL);

    // Loop principal
    while (!WindowShouldClose()) {
        Clay_SetLayoutDimensions((Clay_Dimensions) { GetScreenWidth(), GetScreenHeight() });

        Vector2 mousePos = GetMousePosition();
        Clay_SetPointerState(
            (Clay_Vector2) {
            mousePos.x, mousePos.y
        },
            IsMouseButtonDown(MOUSE_LEFT_BUTTON)
        );

        Clay_BeginLayout();
        CreateUI();
        Clay_RenderCommandArray renderCommands = Clay_EndLayout();

        BeginDrawing();
        ClearBackground((Color) { 23, 23, 28, 255 });

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

    CloseWindow();
    return 0;
}
