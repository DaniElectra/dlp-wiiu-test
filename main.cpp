#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <nn/result.h>
#include <nn/dlp.h>
#include <nn/cfg/CTR.h>

#include <vpad/input.h>
#include <whb/proc.h>
#include <whb/log.h>
#include <whb/log_console.h>

#include <array>
#include <thread>

// Helper macro for printing the raw result
#define RAW_RESULT(res) (reinterpret_cast<NNResult*>(&res)->value)

int
hello_thread()
{
   VPADReadError vpadError;
   VPADStatus vpadStatus;
   WHBLogPrintf("Press A to start distribution");
   WHBLogPrintf("Press B to reboot all clients");
   WHBLogPrintf("Press X to accept all connecting clients");
   WHBLogPrintf("Press Y to disconnect the last client");
   WHBLogPrintf("Press L to show info of the last client");

   std::array<uint16_t, 8> connectingClients;
   uint16_t receivedLength;
   uint16_t lastNodeId = 0;

   while(WHBProcIsRunning()) {
      if (VPADRead(VPAD_CHAN_0, &vpadStatus, 1, &vpadError) != 0 && vpadError == VPAD_READ_SUCCESS) {
         if (vpadStatus.trigger & VPAD_BUTTON_A) {
            // Start distribution of the DLP child to all clients
            nn::Result res = nn::dlp::Cafe::Server::StartDistribution();
            if (res.IsFailure()) {
               WHBLogPrintf("nn::dlp::Cafe::Server::StartDistribution failed! res: %08x, desc: %d, sum: %d", RAW_RESULT(res), res.GetDescription(), res.GetSummary());
            } else {
               WHBLogPrintf("Started distribution! Press B to reboot all clients");
            }
         }

         if (vpadStatus.trigger & VPAD_BUTTON_B) {
            // After all clients received the distribution, we can reboot them into the DLP child
            nn::Result res = nn::dlp::Cafe::Server::RebootAllClients(nullptr);
            if (res.IsFailure()) {
               WHBLogPrintf("nn::dlp::Cafe::Server::RebootAllClients failed! res: %08x, desc: %d, sum: %d", RAW_RESULT(res), res.GetDescription(), res.GetSummary());
            } else {
               WHBLogPrintf("Rebooted all clients!");
            }
         }

         if (vpadStatus.trigger & VPAD_BUTTON_X) {
            // Get all clients connected to the DLP session. Note: The list includes both accepted and not yet accepted clients
            nn::Result res = nn::dlp::Cafe::Server::GetConnectingClients(&receivedLength, connectingClients.data(), connectingClients.size());
            if (res.IsFailure()) {
               WHBLogPrintf("nn::dlp::Cafe::Server::GetConnectingClients failed! res: %08x, desc: %d, sum: %d", RAW_RESULT(res), res.GetDescription(), res.GetSummary());
            } else {
               // Accept all clients into the distribution. This isn't necessary if "isManualAccept" is set to false in "OpenSessions"
               for (int i = 0; i < receivedLength; i++) {
                  const uint16_t nodeId = connectingClients[i];
                  WHBLogPrintf("Accepting node ID %d", nodeId);
                  res = nn::dlp::Cafe::Server::AcceptClient(nodeId);
                  if (res.IsFailure()) {
                     WHBLogPrintf("nn::dlp::Cafe::Server::AcceptClient failed! res: %08x, desc: %d, sum: %d", RAW_RESULT(res), res.GetDescription(), res.GetSummary());
                  } else {
                     lastNodeId = nodeId;
                  }
               }

               WHBLogPrintf("Nodes accepted!");
            }
         }

         if (vpadStatus.trigger & VPAD_BUTTON_Y) {
            // Disconnect the last client that was accepted. This can't be done after distribution has started
            nn::Result res = nn::dlp::Cafe::Server::DisconnectClient(lastNodeId);
            if (res.IsFailure()) {
               WHBLogPrintf("nn::dlp::Cafe::Server::DisconnectClient(%d) failed! res: %08x, desc: %d, sum: %d", lastNodeId, RAW_RESULT(res), res.GetDescription(), res.GetSummary());
            } else {
               WHBLogPrintf("Disconnected node ID %d", lastNodeId);
            }
         }

         if (vpadStatus.trigger & VPAD_BUTTON_L) {
            // Print client state. The "units" seem to be the number of fragments that are transmitted to the client, as it matches with the CIA size divided by the MTU
            nn::dlp::Cafe::ClientState clientState;
            uint32_t unitsTotal;
            uint32_t unitsReceived;
            nn::Result res = nn::dlp::Cafe::Server::GetClientState(&clientState, &unitsTotal, &unitsReceived, lastNodeId);
            if (res.IsFailure()) {
               WHBLogPrintf("nn::dlp::Cafe::Server::GetClientState failed! res: %08x, desc: %d, sum: %d", RAW_RESULT(res), res.GetDescription(), res.GetSummary());
            } else {
               WHBLogPrintf("Node ID: %d, Client state: %d, unitsTotal: %d, unitsReceived: %d", lastNodeId, clientState, unitsTotal, unitsReceived);
            }
         }
      }

      WHBLogConsoleDraw();
   }

   WHBLogPrintf("Exiting... good bye.");
   WHBLogConsoleDraw();
   OSSleepTicks(OSMillisecondsToTicks(1000));
   return 0;
}

int
poll_thread()
{
   nn::dlp::Cafe::ServerState state;

   while(WHBProcIsRunning()) {
      // Poll for changes of the server state
      nn::Result res = nn::dlp::Cafe::Server::PollStateChange(nn::dlp::Cafe::DLP_POLL_NONBLOCK);
      if (RAW_RESULT(res) != 0xBAB253FA) { // LEGACY_DESCRIPTION_NOT_FOUND
         if (res.IsFailure()) {
            WHBLogPrintf("nn::dlp::Cafe::Server::PollStateChange failed! res: %08x, desc: %d, sum: %d", RAW_RESULT(res), res.GetDescription(), res.GetSummary());
         } else {
            WHBLogPrintf("Successfully polled!");
            // If we have polled successfully, get the new *internal* server state
            res = nn::dlp::Cafe::ServerPrivate::GetInternalState(&state);
            if (res.IsFailure()) {
               WHBLogPrintf("nn::dlp::Cafe::ServerPrivate::GetInternalState failed! res: %08x, desc: %d, sum: %d", RAW_RESULT(res), res.GetDescription(), res.GetSummary());
            } else {
               WHBLogPrintf("Polled server state: %d", state);
            }
         }
      }

      OSSleepTicks(OSMillisecondsToTicks(100));
   }

   return 0;
}

int
main(int argc, char **argv)
{
   WHBProcInit();
   WHBLogConsoleInit();

   // Initialize DLP with the parameters for the Mario Kart 7 DLP child and with the username "Wii U", change these to your needs
   nn::cfg::CTR::UserName name = {u"Wii U"};
   bool dupNoticeNeed = false;
   nn::Result res = nn::dlp::Cafe::Server::Initialize(&dupNoticeNeed, 2, 0x000307, 0, &name);
   if (res.IsFailure()) {
      WHBLogPrintf("nn::dlp::Cafe::Server::Initialize failed! res: %08x, desc: %d, sum: %d", RAW_RESULT(res), res.GetDescription(), res.GetSummary());
   } else {
      WHBLogPrintf("nn::dlp::Cafe::Server::Initialize success! dup: %d", dupNoticeNeed);
   }

   // Open the DLP session requiring the clients to be accepted before distribution, and choose the channel automatically
   res = nn::dlp::Cafe::Server::OpenSessions(true, 0);
   if (res.IsFailure()) {
      WHBLogPrintf("nn::dlp::Cafe::Server::OpenSessions failed! res: %08x, desc: %d, sum: %d", RAW_RESULT(res), res.GetDescription(), res.GetSummary());
   } else {
      WHBLogPrintf("DLP session opened for distribution! Distributing Mario Kart 7");
   }

   std::thread t(hello_thread);
   std::thread p(poll_thread);
   t.join();
   p.join();

   // Close the DLP session
   res = nn::dlp::Cafe::Server::CloseSessions();
   if (res.IsFailure()) {
      WHBLogPrintf("nn::dlp::Cafe::Server::CloseSessions failed! res: %08x, desc: %d, sum: %d", RAW_RESULT(res), res.GetDescription(), res.GetSummary());
   }

   // Finalize DLP
   res = nn::dlp::Cafe::Server::Finalize();
   if (res.IsFailure()) {
      WHBLogPrintf("nn::dlp::Cafe::Server::Finalize failed! res: %08x, desc: %d, sum: %d", RAW_RESULT(res), res.GetDescription(), res.GetSummary());
   }

   WHBLogConsoleFree();
   WHBProcShutdown();

   return 0;
}
