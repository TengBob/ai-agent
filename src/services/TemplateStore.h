#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include "database/AgentDao.h"

class TemplateStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList templateNames READ templateNames NOTIFY templatesChanged)

public:
    explicit TemplateStore(QObject *parent = nullptr);

    QStringList templateNames() const;

    Q_INVOKABLE QVariantMap getTemplate(const QString &name);
    Q_INVOKABLE QVariantList getAllTemplates();

signals:
    void templatesChanged();

private:
    void seedBuiltins();
    AgentDao m_dao;
};
