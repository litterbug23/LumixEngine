#include "scriptcompilerwidget.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "ui_scriptcompilerwidget.h"
#include "scriptcompiler.h"
#include "script/script_system.h"
#include <qdesktopservices.h>
#include <qfiledialog.h>
#include <qfilesystemmodel.h>
#include <qprocess.h>
#include <qsettings.h>


static const unsigned int SCRIPT_HASH = crc32("script");


ScriptCompilerWidget::ScriptCompilerWidget(QWidget* parent) 
	: QDockWidget(parent)
	, m_ui(new Ui::ScriptCompilerWidget)
	, m_universe(NULL)
{
	m_ui->setupUi(this);
	m_base_path = QDir::currentPath().toLatin1().data();
	m_compiler = new ScriptCompiler;
	QByteArray base_path = m_base_path.toLatin1();
	m_compiler->setBasePath(Lumix::Path(base_path.data()));
	connect(m_compiler, &ScriptCompiler::compiled, [this](){
		m_ui->compilerOutputView->setText(m_compiler->getLog());
	});
	connect(m_ui->scriptListWidget, &QListWidget::itemDoubleClicked, [this](QListWidgetItem * item) {
		QProcess* process = new QProcess;
		process->start(QString("cmd.exe /C %1/scripts/edit_in_vs.bat %2").arg(m_editor->getBasePath()).arg(item->text()));
		process->connect(process, (void (QProcess::*)(int))&QProcess::finished, [process](int) {
			process->deleteLater();
		});
	});
	QSettings settings("Lumix", "QtEditor");
	m_compiler->setSourcesPath(settings.value("engineSourceCodePath").toString());
	m_ui->engineSourcePathEdit->setText(settings.value("engineSourceCodePath").toString());
	connect(m_ui->engineSourceBrowseButton, &QPushButton::clicked, [this]() {
		m_ui->engineSourcePathEdit->setText(QFileDialog::getExistingDirectory());
		m_compiler->setSourcesPath(m_ui->engineSourcePathEdit->text());
		QSettings settings("Lumix", "QtEditor");
		settings.setValue("engineSourceCodePath", m_ui->engineSourcePathEdit->text());
	});
	connect(m_ui->engineSourcePathEdit, &QLineEdit::editingFinished, [this](){
		m_compiler->setSourcesPath(m_ui->engineSourcePathEdit->text());
		QSettings settings("Lumix", "QtEditor");
		settings.setValue("engineSourceCodePath", m_ui->engineSourcePathEdit->text());
	});
}


ScriptCompilerWidget::~ScriptCompilerWidget()
{
	m_editor->universeCreated().unbind<ScriptCompilerWidget, &ScriptCompilerWidget::onUniverseCreated>(this);
	m_editor->universeDestroyed().unbind<ScriptCompilerWidget, &ScriptCompilerWidget::onUniverseDestroyed>(this);
	m_editor->universeLoaded().unbind<ScriptCompilerWidget, &ScriptCompilerWidget::onUniverseLoaded>(this);
	delete m_compiler;
	delete m_ui;
}


void ScriptCompilerWidget::setWorldEditor(Lumix::WorldEditor& editor)
{
	m_compiler->setWorldEditor(editor);
	m_editor = &editor;
	setUniverse(m_editor->getUniverse());
	m_editor->universeCreated().bind<ScriptCompilerWidget, &ScriptCompilerWidget::onUniverseCreated>(this);
	m_editor->universeDestroyed().bind<ScriptCompilerWidget, &ScriptCompilerWidget::onUniverseDestroyed>(this);
	m_editor->universeLoaded().bind<ScriptCompilerWidget, &ScriptCompilerWidget::onUniverseLoaded>(this);
}


void ScriptCompilerWidget::onUniverseCreated()
{
	setUniverse(m_editor->getUniverse());
}


void ScriptCompilerWidget::onUniverseLoaded()
{
	m_ui->scriptListWidget->clear();
	m_compiler->clearScripts();
	Lumix::ScriptScene* scene = static_cast<Lumix::ScriptScene*>(m_editor->getEngine().getScene(crc32("script")));
	Lumix::Component script = scene->getFirstScript();
	while(script.isValid())
	{
		const Lumix::Path& path = scene->getScriptPath(script);
		m_compiler->addScript(path);
		m_ui->scriptListWidget->addItem(path.c_str());
		script = scene->getNextScript(script);
	}
	QFileInfo info(m_editor->getUniversePath().c_str());
	m_compiler->setProjectName(info.baseName());
}



void ScriptCompilerWidget::onUniverseDestroyed()
{
	setUniverse(NULL);
}


void ScriptCompilerWidget::on_compileAllButton_clicked()
{
	m_compiler->compileAll();
}


void ScriptCompilerWidget::on_openInVSButton_clicked()
{
	QProcess* process = new QProcess;
	process->start(QString("cmd.exe /C %1/scripts/open_in_vs.bat %2.vcxproj").arg(m_editor->getBasePath()).arg(m_compiler->getProjectName()));
	process->connect(process, (void (QProcess::*)(int))&QProcess::finished, [process](int) {
		process->deleteLater();
	});
}


void ScriptCompilerWidget::onComponentCreated(const Lumix::Component& component)
{
	if (component.type == SCRIPT_HASH)
	{
		Lumix::ScriptScene* scene = static_cast<Lumix::ScriptScene*>(m_editor->getEngine().getScene(crc32("script")));
		const Lumix::Path& path = scene->getScriptPath(component);
		m_compiler->addScript(path);
		m_ui->scriptListWidget->addItem(path.c_str());
	}
}


void ScriptCompilerWidget::onComponentDestroyed(const Lumix::Component& component)
{
	if (component.type == SCRIPT_HASH)
	{
		Lumix::ScriptScene* scene = static_cast<Lumix::ScriptScene*>(m_editor->getEngine().getScene(crc32("script")));
		const Lumix::Path& path = scene->getScriptPath(component);
		m_compiler->removeScript(path);
		for (int i = 0; i < m_ui->scriptListWidget->count(); ++i)
		{
			if (m_ui->scriptListWidget->item(i)->text() == path.c_str())
			{
				delete m_ui->scriptListWidget->takeItem(i);
				break;
			}
		}
	}
}


void ScriptCompilerWidget::onScriptRenamed(const Lumix::Path& old_path, const Lumix::Path& new_path)
{
	m_compiler->onScriptRenamed(old_path, new_path);
	for (int i = 0; i < m_ui->scriptListWidget->count(); ++i)
	{
		if (m_ui->scriptListWidget->item(i)->text() == old_path.c_str())
		{
			m_ui->scriptListWidget->item(i)->setText(new_path.c_str());
			break;
		}
	}
}


void ScriptCompilerWidget::setUniverse(Lumix::Universe* universe)
{
	m_universe = universe;
	if (universe)
	{
		Lumix::ScriptScene* scene = static_cast<Lumix::ScriptScene*>(m_editor->getEngine().getScene(crc32("script")));
		scene->scriptRenamed().bind<ScriptCompilerWidget, &ScriptCompilerWidget::onScriptRenamed>(this);
		m_universe->componentCreated().bind<ScriptCompilerWidget, &ScriptCompilerWidget::onComponentCreated>(this);
		m_universe->componentDestroyed().bind<ScriptCompilerWidget, &ScriptCompilerWidget::onComponentDestroyed>(this);
		ASSERT(!scene->getFirstScript().isValid());
	}
	else
	{
		m_ui->scriptListWidget->clear();
		m_compiler->clearScripts();
	}
}
