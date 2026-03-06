import * as vscode from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  Trace,
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;

/** Resolve the endo binary path from settings, defaulting to "endo" (PATH lookup). */
function getEndoPath(): string {
  return vscode.workspace.getConfiguration("endo").get<string>("path", "endo");
}

export async function activate(
  context: vscode.ExtensionContext
): Promise<void> {
  // --- LSP Client ---
  const lspEnabled = vscode.workspace
    .getConfiguration("endo")
    .get<boolean>("lsp.enable", true);

  if (lspEnabled) {
    const serverOptions: ServerOptions = {
      command: getEndoPath(),
      args: ["--lsp"],
    };

    const clientOptions: LanguageClientOptions = {
      documentSelector: [{ scheme: "file", language: "endo" }],
      synchronize: {
        fileEvents: vscode.workspace.createFileSystemWatcher("**/*.endo"),
      },
    };

    client = new LanguageClient(
      "endo",
      "Endo Language Server",
      serverOptions,
      clientOptions
    );

    const traceLevel = vscode.workspace
      .getConfiguration("endo")
      .get<string>("lsp.trace", "off");
    if (traceLevel !== "off") {
      client.setTrace(
        traceLevel === "verbose" ? Trace.Verbose : Trace.Messages
      );
    }

    client.start();
  }

  // --- DAP Adapter ---
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory("endo", {
      createDebugAdapterDescriptor(
        _session: vscode.DebugSession
      ): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
        return new vscode.DebugAdapterExecutable(getEndoPath(), ["--dap"]);
      },
    })
  );

  // --- Debug Configuration Provider ---
  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider("endo", {
      resolveDebugConfiguration(
        _folder: vscode.WorkspaceFolder | undefined,
        config: vscode.DebugConfiguration
      ): vscode.ProviderResult<vscode.DebugConfiguration> {
        // When the user presses F5 without a launch.json, config is nearly empty.
        if (!config.type && !config.request && !config.name) {
          const editor = vscode.window.activeTextEditor;
          if (editor && editor.document.languageId === "endo") {
            config.type = "endo";
            config.request = "launch";
            config.name = "Launch Endo Script";
            config.program = "${file}";
          }
        }

        if (!config.program) {
          return vscode.window
            .showErrorMessage("Cannot launch: no program specified")
            .then(() => undefined);
        }

        return config;
      },
    })
  );
}

export async function deactivate(): Promise<void> {
  if (client) {
    await client.stop();
  }
}
