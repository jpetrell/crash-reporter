/*
 * This file is part of crash-reporter
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Ville Ilvonen <ville.p.ilvonen@nokia.com>
 * Author: Riku Halonen <riku.halonen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QVariant>
#include <QDBusConnection>
#include <QDBusConnectionInterface>

#include "ut_creporterdaemonmonitor.h"
#include "creportercoreregistry.h"
#include "creportertestutils.h"
#include "creporterdaemonmonitor.h"
#include "creporternamespace.h"
#include "creporterdialogserverdbusadaptor.h"
#include "creporterdaemonmonitor_p.h"
#include "creporternotification.h"

static bool notificationCreated;
static bool notificationUpdated;

static bool serviceRegisteredReply;
// Overridden here to able to fake UI launch failing. Otherwise the Qt implementation seems
// to return true always in unit tests.
QDBusReply<bool> QDBusConnectionInterface::isServiceRegistered(const QString &serviceName) const
{
    Q_UNUSED(serviceName);
    QDBusMessage reply;
    reply << serviceRegisteredReply;
    return reply;
}

// CReporterNotification mock object.
CReporterNotification::CReporterNotification(const QString &eventType,
                                             const QString &summary, const QString &body,
                                             QObject *parent)
{
    Q_UNUSED(eventType);
    Q_UNUSED(summary);
    Q_UNUSED(body);
    notificationCreated = true;
    Q_UNUSED(parent);
}

CReporterNotification::CReporterNotification(const QString &eventType, int id,
                                             QObject *parent)
{
    Q_UNUSED(eventType);
    Q_UNUSED(id);
    Q_UNUSED(parent);

    notificationCreated = true;
}

CReporterNotification::~CReporterNotification()
{}

int CReporterNotification::id()
{
    return 10;
}

void CReporterNotification::update(const QString &summary, const QString &body,
                                   int count)
{
    notificationUpdated = true;
    Q_UNUSED(body);
    Q_UNUSED(count)
    Q_UNUSED(summary);
}

void CReporterNotification::remove()
{}

void CReporterNotification::setTimeout(int ms)
{
    Q_UNUSED(ms);
}

TestDialogServer::TestDialogServer()
{
    callReceivedCalled = false;
    quitCalled = false;

    new CReporterDialogServerDBusAdaptor(this);
    QDBusConnection::sessionBus().registerObject(CReporter::DialogServerObjectPath, this);
    QDBusConnection::sessionBus().registerService(CReporter::DialogServerServiceName);
}

TestDialogServer::~TestDialogServer()
{
}

QDBusError::ErrorType TestDialogServer::callReceived(const QString &dialogName,
                                const QVariantList &arguments, const QDBusMessage &message) {

    requestedDialog = dialogName;
    callArguments = arguments;
    requestMessage = message;
    callReceivedCalled = true;

    return QDBusError::NoError;
}

void TestDialogServer::quit()
{
    quitCalled = true;
}

void Ut_CReporterDaemonMonitor::initTestCase()
{
    CReporterTestUtils::createTestMountpoints();
}

void Ut_CReporterDaemonMonitor::init()
{
    notificationCreated = false;
    notificationUpdated = false;
    serviceRegisteredReply = true;

    paths = CReporterCoreRegistry::instance()->getCoreLocationPaths();

    testDialogServer = new TestDialogServer();
}

void Ut_CReporterDaemonMonitor::testNewCoreFileFoundNotified()
{
    // Check that rich-core file is sent to the UI.
    monitor = new CReporterDaemonMonitor(this);
	
    QSignalSpy richCoreNotifySpy(monitor, SIGNAL(richCoreNotify(QString)));

    QString filePath(paths.at(0));
    filePath.append("/test-1234-11-4321.rcore.lzo");

    QDir::setCurrent(paths.at(0));
	
	QFile file;
    file.setFileName("test-1234-11-4321.rcore.lzo");
    file.open(QIODevice::ReadWrite);
    file.close();

    QTest::qWait(100);

    QVERIFY(testDialogServer->callReceivedCalled == true);
    QVERIFY(testDialogServer->callArguments.at(0).type() == QVariant::String);
    QVERIFY(testDialogServer->requestedDialog == CReporter::NotifyNewDialogType);
    QString argument = testDialogServer->callArguments.at(0).toString();
    QCOMPARE(argument, filePath);

    QVERIFY(richCoreNotifySpy.count() == 1);
}

void Ut_CReporterDaemonMonitor::testNewCoreFileFoundInvalidFile()
{
    monitor = new CReporterDaemonMonitor(this);
		
    QString filePath(paths.at(0));
    filePath.append("/application-4321-10-1111.txt");

    QDir::setCurrent(paths.at(0));
	
	QFile file;
    file.setFileName("application-4321-10-1111.txt");
    file.open(QIODevice::ReadWrite);
    file.close();

    QTest::qWait(50);

    QVERIFY(testDialogServer->callReceivedCalled == false);
}

void Ut_CReporterDaemonMonitor::testNewCoreFileFoundByTheSameName()
{
    monitor = new CReporterDaemonMonitor(this);

    QString filePath(paths.at(0));
    filePath.append("/mytest-1234-11-4321.rcore.lzo");

    QDir::setCurrent(paths.at(0));
	
	QFile file;
    file.setFileName("mytest-1234-11-4321.rcore.lzo");
    file.open(QIODevice::ReadWrite);
    file.close();

    QTest::qWait(50);

    QVERIFY(testDialogServer->callReceivedCalled == true);
    QVERIFY(testDialogServer->callArguments.at(0).type() == QVariant::String);
    QVERIFY(testDialogServer->requestedDialog == CReporter::NotifyNewDialogType);
    QString argument = testDialogServer->callArguments.at(0).toString();
    QCOMPARE(argument, filePath);

    file.remove();

    QTest::qWait(50);

    testDialogServer->callReceivedCalled = false;

    QString dubFilePath(paths.at(0));
    dubFilePath.append("/mytest-1234-11-4321.rcore.lzo");

    QDir::setCurrent(paths.at(0));
	
	QFile dubFile;
    dubFile.setFileName("mytest-1234-11-4321.rcore.lzo");
    dubFile.open(QIODevice::ReadWrite);
    dubFile.close();
	
    QTest::qWait(50);

    QVERIFY(testDialogServer->callReceivedCalled == true);
    QVERIFY(testDialogServer->callArguments.at(0).type() == QVariant::String);
    QVERIFY(testDialogServer->requestedDialog == CReporter::NotifyNewDialogType);
    argument = testDialogServer->callArguments.at(0).toString();
    QCOMPARE(argument, dubFilePath);
}

void Ut_CReporterDaemonMonitor::testDirectoryDeletedNotNotified()
{
    monitor = new CReporterDaemonMonitor(this);

    CReporterTestUtils::removeDirectory(paths.at(0));

    QTest::qWait(50);
    QVERIFY(testDialogServer->callReceivedCalled == false);
}

void Ut_CReporterDaemonMonitor::testAutoDeleteDublicateCores()
{
    // Check, that file is deleted automatically after it has handled maximum number of times (10).   
    monitor = new CReporterDaemonMonitor(this);
    monitor->setAutoDelete(true);

    QVERIFY(monitor->autoDeleteEnabled() == true);

    QString filePath(paths.at(0));
    filePath.append("/crashapplication-0287-11-2260.rcore.lzo");

    int counter = 1;
    while (counter <= monitor->autoDeleteMaxSimilarCores()) {
        QFile::remove(filePath);
        QTest::qWait(100);
        QFile::copy("/usr/lib/crash-reporter-tests/testdata/crashapplication-0287-11-2260.rcore.lzo", filePath);

        QVERIFY(QFile::exists(filePath) == true);
        counter++;
         QTest::qWait(500);
    }

    QVERIFY(QFile::exists(filePath) == false);
}

void Ut_CReporterDaemonMonitor::testUIFailedToLaunch()
{
    // Test situation, where UI is tried to launch for notification, but fails.
    // Instead error notification is displayed.
    serviceRegisteredReply = false;

    delete testDialogServer;
    testDialogServer = 0;

    monitor = new CReporterDaemonMonitor(this);

    QString filePath(paths.at(0));
    filePath.append("/test-1234-11-4321.rcore.lzo");

    QDir::setCurrent(paths.at(0));

    QFile file;
    file.setFileName("test-1234-11-4321.rcore.lzo");
    file.open(QIODevice::ReadWrite);
    file.close();

    QTest::qWait(50);
    QVERIFY(notificationCreated == true);
}

void Ut_CReporterDaemonMonitor::cleanupTestCase()
{
    CReporterTestUtils::removeTestMountpoints();
}

void Ut_CReporterDaemonMonitor::cleanup()
{
    CReporterTestUtils::removeDirectories(paths);

    if (testDialogServer != 0) {
        delete testDialogServer;
        testDialogServer = 0;
    }

}

QTEST_MAIN(Ut_CReporterDaemonMonitor)

