#include "TemplateStore.h"

static const struct { const char *name, *desc, *prompt, *provider, *model; double temp; }
kBuiltins[] = {
    {"通用助手",   "通用 AI 助手",
     "你是一个有用的 AI 助手，请用中文回答。",
     "openai",   "gpt-4o-mini",    0.7},
    {"代码助手",   "资深程序员",
     "你是资深程序员，擅长写代码、debug、代码审查，回答要包含代码示例。",
     "deepseek", "deepseek-chat",  0.3},
    {"写作助手",   "专业作家",
     "你是专业作家，帮助润色、改写、创作各类文本，风格流畅自然。",
     "openai",   "gpt-4o",         0.8},
    {"翻译专家",   "精通中英互译",
     "你是专业翻译，精通中英互译，保持原意和风格，只输出翻译结果。",
     "openai",   "gpt-4o-mini",    0.3},
    {"数据分析师", "数据分析专家",
     "你是数据分析专家，擅长解读数据、趋势分析，提供量化洞察。",
     "deepseek", "deepseek-chat",  0.5},
    {"学习导师",   "Socratic 学习引导",
     "你是耐心导师，用 Socratic 方法引导学习，多问问题，让用户自己思考。",
     "openai",   "gpt-4o",         0.7},
};

TemplateStore::TemplateStore(QObject *parent) : QObject(parent)
{
    seedBuiltins();
}

void TemplateStore::seedBuiltins()
{
    if (m_dao.templateCount() > 0) return;
    for (const auto &b : kBuiltins) {
        TemplateData t;
        t.name         = QString::fromUtf8(b.name);
        t.description  = QString::fromUtf8(b.desc);
        t.systemPrompt = QString::fromUtf8(b.prompt);
        t.provider     = QString::fromUtf8(b.provider);
        t.modelName    = QString::fromUtf8(b.model);
        t.temperature  = b.temp;
        t.isBuiltin    = true;
        m_dao.insertTemplate(t);
    }
}

QStringList TemplateStore::templateNames() const
{
    const auto all = const_cast<TemplateStore*>(this)->m_dao.getAllTemplates();
    QStringList names;
    for (const auto &t : all) names.append(t.name);
    return names;
}

QVariantMap TemplateStore::getTemplate(const QString &name)
{
    const TemplateData t = m_dao.getTemplateByName(name);
    return {
        {"name",         t.name},
        {"description",  t.description},
        {"systemPrompt", t.systemPrompt},
        {"provider",     t.provider},
        {"modelName",    t.modelName},
        {"temperature",  t.temperature},
    };
}

QVariantList TemplateStore::getAllTemplates()
{
    const auto all = m_dao.getAllTemplates();
    QVariantList result;
    for (const auto &t : all)
        result.append(QVariantMap{
            {"name",         t.name},
            {"description",  t.description},
            {"systemPrompt", t.systemPrompt},
            {"provider",     t.provider},
            {"modelName",    t.modelName},
            {"temperature",  t.temperature},
        });
    return result;
}
