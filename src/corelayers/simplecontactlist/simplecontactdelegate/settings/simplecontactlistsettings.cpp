#include "simplecontactlistsettings.h"
#include "ui_simplecontactlistsettings.h"
#include <qutim/config.h>
#include <qutim/localizedstring.h>
#include <qutim/status.h>
#include <qutim/protocol.h>

namespace Core
{

typedef QMap<int,LocalizedString> SizeMap;

void size_map_init(SizeMap &map)
{
	//map.insert(-1,QT_TRANSLATE_NOOP("ContactList","Other"));
	map.insert(0,QT_TRANSLATE_NOOP("ContactList","Default (depends on platform)"));
	map.insert(16,QT_TRANSLATE_NOOP("ContactList","Small (16x16)"));
	map.insert(22,QT_TRANSLATE_NOOP("ContactList","Medium (22x22)"));
	map.insert(32,QT_TRANSLATE_NOOP("ContactList","Large (32x32)"));
	map.insert(48,QT_TRANSLATE_NOOP("ContactList","Very large (48x48)"));
	map.insert(64,QT_TRANSLATE_NOOP("ContactList","Huge (64x64)"));
}

SimpleContactlistSettings::SimpleContactlistSettings() :
		ui(new Ui::SimpleContactlistSettings)
{
	ui->setupUi(this);
	connect(ui->avatarsBox,SIGNAL(toggled(bool)),SLOT(onAvatarBoxToggled(bool)));
	connect(ui->extendedInfoBox,SIGNAL(toggled(bool)),SLOT(onExtendedInfoBoxToggled(bool)));
	connect(ui->statusBox,SIGNAL(toggled(bool)),SLOT(onStatusBoxToggled(bool)));
	connect(ui->sizesBox,SIGNAL(currentIndexChanged(int)),SLOT(onModified()));
	// Load extended statuses list
	foreach (Protocol *protocol, Protocol::all()) {
		ExtendedInfosEvent event;
		qApp->sendEvent(protocol, &event);
		foreach (const QVariantHash &info, event.infos()) {
			QString id = info.value("id").toString();
			if (id.isEmpty() || m_statusesBoxes.contains(id))
				continue;
			QString desc = info.value("settingsDescription").toString();
			if (desc.isEmpty())
				desc = QString("Show '%' icon").arg(id);
			QCheckBox *box = new QCheckBox(desc, this);
			box->setObjectName(id);
			connect(ui->extendedInfoBox, SIGNAL(toggled(bool)), box, SLOT(setEnabled(bool)));
			connect(box, SIGNAL(clicked()), SLOT(onModified()));
			ui->layout->addRow(box);
			m_statusesBoxes.insert(id, box);
		}
	}
}

SimpleContactlistSettings::~SimpleContactlistSettings()
{
	delete ui;
}

void SimpleContactlistSettings::cancelImpl()
{

}

void SimpleContactlistSettings::loadImpl()
{
	reloadCombobox();
	Config config = Config("appearance").group("contactList");
	m_flags = static_cast<ContactDelegate::ShowFlags>(config.value("showFlags",
															ContactDelegate::ShowStatusText |
															ContactDelegate::ShowExtendedInfoIcons |
															ContactDelegate::ShowAvatars));
	// Load extendes statuses
	config.beginGroup("extendedStatuses");
	foreach (QCheckBox *checkBox, m_statusesBoxes) {
		bool checked = config.value(checkBox->objectName(), true);
		checkBox->setChecked(checked);
	}
	config.endGroup();

	int size = config.value("statusIconSize",0);
	SizeMap::const_iterator it;
	int index = -1;
	for (int i = 0;i!=ui->sizesBox->count();i++) {
		if (size == ui->sizesBox->itemData(i).toInt()) {
			index = i;
			break;
		}
	}
	if (index == -1) {
		index = 0;
		//other size (TODO)
	}
	else
		ui->sizesBox->setCurrentIndex(index);
	ui->avatarsBox->setChecked(m_flags & ContactDelegate::ShowAvatars);
	ui->extendedInfoBox->setChecked(m_flags & ContactDelegate::ShowExtendedInfoIcons);
	ui->statusBox->setChecked(m_flags & ContactDelegate::ShowStatusText);
}

void SimpleContactlistSettings::saveImpl()
{
	Config config = Config("appearance").group("contactList");
	config.setValue("showFlags",m_flags);
	int size = ui->sizesBox->itemData(ui->sizesBox->currentIndex()).toInt();
	if (size == 0)
		config.remove("statusIconSize");
	else
		config.setValue("statusIconSize",size);
	// Save extended statuses
	config.beginGroup("extendedStatuses");
	foreach (QCheckBox *checkBox, m_statusesBoxes)
		config.setValue(checkBox->objectName(), checkBox->isChecked());
	config.endGroup();
}

void SimpleContactlistSettings::reloadCombobox()
{
	ui->sizesBox->clear();
	SizeMap sizeMap;
	size_map_init(sizeMap);
	SizeMap::const_iterator it;
	for (it = sizeMap.constBegin();it!=sizeMap.constEnd();it++) {
		ui->sizesBox->addItem(it->toString());
		ui->sizesBox->setItemData(ui->sizesBox->count()-1,it.key());
	}
}

void SimpleContactlistSettings::setFlag(ContactDelegate::ShowFlags flag, bool on)
{
	if (on) 
		m_flags |= flag;
	else 
		m_flags &= ~flag;
}

void SimpleContactlistSettings::onAvatarBoxToggled(bool toggled)
{
	setFlag(ContactDelegate::ShowAvatars,toggled);
	emit modifiedChanged(true);
}
void SimpleContactlistSettings::onExtendedInfoBoxToggled(bool toggled)
{
	setFlag(ContactDelegate::ShowExtendedInfoIcons,toggled);
	emit modifiedChanged(true);
}
void SimpleContactlistSettings::onStatusBoxToggled(bool toggled)
{
	setFlag(ContactDelegate::ShowStatusText,toggled);
	emit modifiedChanged(true);
}

void SimpleContactlistSettings::onModified()
{
	emit modifiedChanged(true);
}

}
