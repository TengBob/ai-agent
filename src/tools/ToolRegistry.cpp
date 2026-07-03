#include "ToolRegistry.h"
#include <QJsonObject>

ToolRegistry &ToolRegistry::instance()
{
    static ToolRegistry inst;
    return inst;
}

ToolRegistry::ToolRegistry()
{
    // web_search
    registerTool({
        "web_search",
        "Search the web for up-to-date information. Use this when you need current facts, news, or data not in your training.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"query", QJsonObject{{"type","string"},{"description","The search query"}}},
                {"max_results", QJsonObject{{"type","integer"},{"description","Max results to return (1-10)"},{"default",5}}}
            }},
            {"required", QJsonArray{"query"}}
        }
    });

    // read_file
    registerTool({
        "read_file",
        "Read the contents of a local file. Returns the file text.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"path", QJsonObject{{"type","string"},{"description","Absolute or relative file path"}}}
            }},
            {"required", QJsonArray{"path"}}
        }
    });

    // write_file
    registerTool({
        "write_file",
        "Write text content to a local file. Creates the file if it does not exist.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"path",    QJsonObject{{"type","string"},{"description","File path to write"}}},
                {"content", QJsonObject{{"type","string"},{"description","Text content to write"}}},
                {"append",  QJsonObject{{"type","boolean"},{"description","Append instead of overwrite"},{"default",false}}}
            }},
            {"required", QJsonArray{"path","content"}}
        }
    });

    // run_command
    registerTool({
        "run_command",
        "Execute a shell command or script and return its stdout/stderr output. Use for running Python, reading system info, etc.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"command",    QJsonObject{{"type","string"},{"description","The command to run"}}},
                {"timeout_ms", QJsonObject{{"type","integer"},{"description","Timeout in milliseconds"},{"default",10000}}}
            }},
            {"required", QJsonArray{"command"}}
        }
    });

    // http_request
    registerTool({
        "http_request",
        "Make an HTTP request to any URL and return the response body. Useful for calling external APIs.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"url",     QJsonObject{{"type","string"},{"description","Request URL"}}},
                {"method",  QJsonObject{{"type","string"},{"description","HTTP method"},{"enum",QJsonArray{"GET","POST","PUT","DELETE"}},{"default","GET"}}},
                {"headers", QJsonObject{{"type","object"},{"description","Optional request headers"}}},
                {"body",    QJsonObject{{"type","string"},{"description","Request body for POST/PUT"}}}
            }},
            {"required", QJsonArray{"url"}}
        }
    });

    // convert_pdf_to_html
    registerTool({
        "convert_pdf_to_html",
        "Convert a local PDF file to a self-contained HTML document and save it to the knowledge base. "
        "Returns the path to the generated index.html.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"pdf_path",    QJsonObject{{"type","string"},{"description","Absolute or relative path to the PDF file"}}},
                {"output_name", QJsonObject{{"type","string"},{"description","Optional display title for the document"}}}
            }},
            {"required", QJsonArray{"pdf_path"}}
        }
    });

    // read_html_page
    registerTool({
        "read_html_page",
        "Read the text content of a specific page from a knowledge-base HTML file. "
        "Only use this when the user explicitly asks about a different page than the one currently shown; "
        "the current page content is already provided in the conversation context.",
        QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"file_path", QJsonObject{{"type","string"},{"description","Absolute path to the HTML index.html file"}}},
                {"page",      QJsonObject{{"type","integer"},{"description","1-based page number to read"}}}
            }},
            {"required", QJsonArray{"file_path","page"}}
        }
    });
}

void ToolRegistry::registerTool(const ToolDef &def)
{
    m_tools.append(def);
}

QJsonArray ToolRegistry::toOpenAITools() const
{
    QJsonArray arr;
    for (const ToolDef &t : m_tools) {
        arr.append(QJsonObject{
            {"type", "function"},
            {"function", QJsonObject{
                {"name",        t.name},
                {"description", t.description},
                {"parameters",  t.parameters}
            }}
        });
    }
    return arr;
}

QStringList ToolRegistry::toolNames() const
{
    QStringList names;
    for (const ToolDef &t : m_tools) names << t.name;
    return names;
}
