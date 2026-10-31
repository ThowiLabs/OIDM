#pragma once

#include <QObject>

class DownloadTaskTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void downloadsFullResponse();
    void segmentedTransfersReassembleLargeFiles();
    void fallsBackWhenSegmentRangeIsIgnored();
    void segmentedTransferResumesSavedFragments();
    void resumesFromValidatedRange();
    void restartsWhenIfRangeValidatorChanges();
    void restartsWhenServerIgnoresRange();
    void rejectsMismatchedContentRange();
    void preservesPartialFileAfterInterruptedResponse();
    void cancelsAndCleansTemporaryFiles();
    void rejectsExistingDestination();
    void rejectsEmbeddedCredentials();
    void publishesWithoutReplacingDestination();
    void doesNotFollowTemporaryDirectorySymlinkOnCancel();
    void protectsDestinationCreatedDuringTransfer();
    void pausesAndResumesSingleStream();
    void limitsConcurrentDownloadsAndStartsQueuedItems();
};
