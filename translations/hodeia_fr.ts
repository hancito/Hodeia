<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="fr-FR">
<context>
    <name>AcpClient</name>
    <message>
        <source>the agent stopped unexpectedly</source>
        <translation type="vanished">l’agent s’est arrêté de façon inattendue</translation>
    </message>
    <message>
        <source>the agent exited (code %1)</source>
        <translation type="vanished">l’agent s’est terminé (code %1)</translation>
    </message>
    <message>
        <source>No command to start the agent.</source>
        <translation type="vanished">Aucune commande pour lancer l’agent.</translation>
    </message>
    <message>
        <source>Cannot start &quot;%1&quot;: %2</source>
        <translation type="vanished">Impossible de lancer « %1 » : %2</translation>
    </message>
    <message>
        <source>the agent was stopped</source>
        <translation type="vanished">l’agent a été arrêté</translation>
    </message>
    <message>
        <source>the agent is not running</source>
        <translation type="vanished">l’agent n’est pas lancé</translation>
    </message>
</context>
<context>
    <name>AgentBackend</name>
    <message>
        <source>Claude Code is not logged in: run &quot;claude&quot; in a terminal and log in (/login), then try again.
(%1)</source>
        <translation type="vanished">Claude Code n’est pas connecté : lancez « claude » dans un terminal et connectez-vous (/login), puis réessayez.
(%1)</translation>
    </message>
    <message>
        <source>%1
Install the agent with &quot;npm install -g @agentclientprotocol/claude-agent-acp&quot;, or set its command in the Backends window.</source>
        <translation type="vanished">%1
Installez l’agent avec « npm install -g @agentclientprotocol/claude-agent-acp », ou indiquez sa commande dans la fenêtre Backends.</translation>
    </message>
    <message>
        <source>The agent did not start: %1</source>
        <translation type="vanished">L’agent n’a pas démarré : %1</translation>
    </message>
    <message>
        <source>no answer</source>
        <translation type="vanished">pas de réponse</translation>
    </message>
    <message>
        <source>The agent refused the model &quot;%1&quot;: %2</source>
        <translation type="vanished">L’agent a refusé le modèle « %1 » : %2</translation>
    </message>
    <message>
        <location filename="../src/agentbackend.cpp" line="35"/>
        <source>Unknown agent &quot;%1&quot;: choose one in the Backends window.</source>
        <translation>Agent inconnu « %1 » : choisissez-en un dans la fenêtre Backends.</translation>
    </message>
    <message>
        <location filename="../src/agentbackend.cpp" line="40"/>
        <source>No connector &quot;%1&quot; for the agent %2.</source>
        <translation>Aucun connecteur « %1 » pour l’agent %2.</translation>
    </message>
    <message>
        <location filename="../src/agentbackend.cpp" line="79"/>
        <source>The agent failed.</source>
        <translation>L’agent a échoué.</translation>
    </message>
    <message>
        <location filename="../src/agentbackend.cpp" line="81"/>
        <source>The agent declined the request.</source>
        <translation>L’agent a refusé la demande.</translation>
    </message>
    <message>
        <location filename="../src/agentbackend.cpp" line="167"/>
        <source>The backend &quot;%1&quot; does not say which agent it uses: choose the agent again in the Backends window and check the connection.</source>
        <translation>Le backend « %1 » n’indique pas quel agent il utilise : choisissez de nouveau l’agent dans la fenêtre Backends et vérifiez la connexion.</translation>
    </message>
    <message>
        <source>Unknown agent &quot;%1&quot;.</source>
        <translation type="vanished">Agent inconnu « %1 ».</translation>
    </message>
    <message>
        <location filename="../src/agentbackend.cpp" line="175"/>
        <source>No connector &quot;%1&quot;.</source>
        <translation>Aucun connecteur « %1 ».</translation>
    </message>
    <message>
        <source>The agent stopped without answering (%1).</source>
        <translation type="vanished">L’agent s’est arrêté sans répondre (%1).</translation>
    </message>
</context>
<context>
    <name>AgentChatController</name>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="244"/>
        <source>The message could not be saved: %1</source>
        <translation>Le message n’a pas pu être enregistré : %1</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="256"/>
        <source>No agent can search the library: add Claude Code or another agent in the Backends window.</source>
        <translation>Aucun agent ne peut chercher dans la bibliothèque : ajoutez Claude Code ou un autre agent dans la fenêtre Backends.</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="271"/>
        <source>The conversation could not be created: %1</source>
        <translation>La conversation n’a pas pu être créée : %1</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="295"/>
        <source>The agent is working...</source>
        <translation>L’agent travaille…</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="308"/>
        <source>The agent backend &quot;%1&quot; is not available: check the Backends window.</source>
        <translation>Le backend d’agent « %1 » n’est pas disponible : vérifiez la fenêtre Backends.</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="332"/>
        <source>Starting the agent...</source>
        <translation>Démarrage de l’agent…</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="364"/>
        <source>Searching the library...</source>
        <translation>Recherche dans la bibliothèque…</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="366"/>
        <source>Reading a page...</source>
        <translation>Lecture d’une page…</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="368"/>
        <source>Reading a summary...</source>
        <translation>Lecture d’un résumé…</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="370"/>
        <source>Listing the papers...</source>
        <translation>Liste des articles…</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="372"/>
        <source>Looking at the citations...</source>
        <translation>Examen des citations…</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="374"/>
        <source>Using %1...</source>
        <translation>Utilisation de %1…</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="378"/>
        <source>Thinking...</source>
        <translation>Réflexion…</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="381"/>
        <source>Writing the answer...</source>
        <translation>Rédaction de la réponse…</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="411"/>
        <source>The agent failed.</source>
        <translation>L’agent a échoué.</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="415"/>
        <source>(stopped)</source>
        <translation>(arrêté)</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="418"/>
        <location filename="../src/agentchatcontroller.cpp" line="450"/>
        <source>Stopped before answering.</source>
        <translation>Arrêté avant de répondre.</translation>
    </message>
    <message>
        <location filename="../src/agentchatcontroller.cpp" line="454"/>
        <source>Stopping...</source>
        <translation>Arrêt…</translation>
    </message>
</context>
<context>
    <name>Agents</name>
    <message>
        <location filename="../src/agents/agentsync.cpp" line="43"/>
        <source>The agent did not answer.</source>
        <translation>L’agent n’a pas répondu.</translation>
    </message>
</context>
<context>
    <name>Agents::AcpConnection</name>
    <message>
        <location filename="../src/agents/connectors/acp/acpconnector.cpp" line="107"/>
        <source>%1 is not logged in.</source>
        <translation>%1 n’est pas connecté.</translation>
    </message>
    <message>
        <location filename="../src/agents/connectors/acp/acpconnector.cpp" line="109"/>
        <source>Log in with: %1</source>
        <translation>Connectez-vous avec : %1</translation>
    </message>
    <message>
        <source>Install it with: %1</source>
        <translation type="vanished">Installez-le avec : %1</translation>
    </message>
    <message>
        <location filename="../src/agents/connectors/acp/acpconnector.cpp" line="140"/>
        <source>The agent did not start: %1</source>
        <translation>L’agent n’a pas démarré : %1</translation>
    </message>
    <message>
        <location filename="../src/agents/connectors/acp/acpconnector.cpp" line="214"/>
        <source>The agent refused the model &quot;%1&quot;: %2</source>
        <translation>L’agent a refusé le modèle « %1 » : %2</translation>
    </message>
    <message>
        <location filename="../src/agents/connectors/acp/acpconnector.cpp" line="227"/>
        <source>No open session.</source>
        <translation>Aucune session ouverte.</translation>
    </message>
    <message>
        <location filename="../src/agents/connectors/acp/acpconnector.cpp" line="249"/>
        <source>The agent declined the request.</source>
        <translation>L’agent a refusé la demande.</translation>
    </message>
    <message>
        <location filename="../src/agents/connectors/acp/acpconnector.cpp" line="258"/>
        <source>Cancelled.</source>
        <translation>Annulé.</translation>
    </message>
</context>
<context>
    <name>Agents::AgentProcess</name>
    <message>
        <location filename="../src/agents/agentprocess.cpp" line="62"/>
        <source>%1 needs %2.</source>
        <translation>%1 nécessite %2.</translation>
    </message>
    <message>
        <location filename="../src/agents/agentprocess.cpp" line="65"/>
        <source>%1 is not installed on this computer.</source>
        <translation>%1 n’est pas installé sur cet ordinateur.</translation>
    </message>
    <message>
        <location filename="../src/agents/agentprocess.cpp" line="72"/>
        <source>Install it with: %1</source>
        <translation>Installez-le avec : %1</translation>
    </message>
    <message>
        <location filename="../src/agents/agentprocess.cpp" line="96"/>
        <source>the agent stopped unexpectedly</source>
        <translation>l’agent s’est arrêté de façon inattendue</translation>
    </message>
    <message>
        <location filename="../src/agents/agentprocess.cpp" line="99"/>
        <source>the agent exited (code %1)</source>
        <translation>l’agent s’est terminé (code %1)</translation>
    </message>
    <message>
        <location filename="../src/agents/agentprocess.cpp" line="133"/>
        <source>No command to start the agent.</source>
        <translation>Aucune commande pour lancer l’agent.</translation>
    </message>
    <message>
        <location filename="../src/agents/agentprocess.cpp" line="150"/>
        <source>Cannot start &quot;%1&quot;: %2</source>
        <translation>Impossible de lancer « %1 » : %2</translation>
    </message>
</context>
<context>
    <name>Agents::ClaudeConnection</name>
    <message>
        <location filename="../src/agents/connectors/claude/claudeconnector.cpp" line="27"/>
        <source>The agent was stopped.</source>
        <translation>L’agent a été arrêté.</translation>
    </message>
    <message>
        <source>Install it with: %1</source>
        <translation type="vanished">Installez-le avec : %1</translation>
    </message>
    <message>
        <location filename="../src/agents/connectors/claude/claudeconnector.cpp" line="114"/>
        <source>No open session.</source>
        <translation>Aucune session ouverte.</translation>
    </message>
    <message>
        <location filename="../src/agents/connectors/claude/claudeconnector.cpp" line="172"/>
        <source>Claude usage limit reached.</source>
        <translation>Limite d’utilisation de Claude atteinte.</translation>
    </message>
</context>
<context>
    <name>Agents::PiConnection</name>
    <message>
        <source>Install it with: %1</source>
        <translation type="vanished">Installez-le avec : %1</translation>
    </message>
    <message>
        <location filename="../src/agents/connectors/pi/piconnector.cpp" line="91"/>
        <source>No open session.</source>
        <translation>Aucune session ouverte.</translation>
    </message>
</context>
<context>
    <name>AnthropicApi</name>
    <message>
        <location filename="../src/anthropicapi.cpp" line="96"/>
        <source>HTTP error %1</source>
        <translation>Erreur HTTP %1</translation>
    </message>
    <message>
        <location filename="../src/anthropicapi.cpp" line="98"/>
        <source>JSON parse error: %1</source>
        <translation>Erreur d’analyse JSON : %1</translation>
    </message>
    <message>
        <location filename="../src/anthropicapi.cpp" line="117"/>
        <source>The model declined the request (%1)</source>
        <translation>Le modèle a refusé la requête (%1)</translation>
    </message>
    <message>
        <location filename="../src/anthropicapi.cpp" line="132"/>
        <source>Empty response from server</source>
        <translation>Réponse vide du serveur</translation>
    </message>
</context>
<context>
    <name>BackendWindow</name>
    <message>
        <location filename="../ui/BackendWindow.qml" line="10"/>
        <source>Backend Management</source>
        <translation>Gestion des backends</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="41"/>
        <source>Connected — %1 model(s) available</source>
        <translation>Connecté — %1 modèle(s) disponible(s)</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="45"/>
        <source>Connection failed: %1</source>
        <translation>Échec de la connexion : %1</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="60"/>
        <source>Backends</source>
        <translation>Backends</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="80"/>
        <source>Edit</source>
        <translation>Modifier</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="94"/>
        <source>Remove</source>
        <translation>Supprimer</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="112"/>
        <source>Edit Backend</source>
        <translation>Modifier le backend</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="112"/>
        <location filename="../ui/BackendWindow.qml" line="265"/>
        <source>Add Backend</source>
        <translation>Ajouter un backend</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="121"/>
        <source>Name</source>
        <translation>Nom</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="128"/>
        <source>my-backend</source>
        <translation>mon-backend</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="135"/>
        <source>API</source>
        <translation>API</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="141"/>
        <source>OpenAI-compatible</source>
        <translation>Compatible OpenAI</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="141"/>
        <source>Anthropic</source>
        <translation>Anthropic</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="141"/>
        <source>Agent (Claude Code, Qwen, Pi...)</source>
        <translation>Agent (Claude Code, Qwen, Pi…)</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="150"/>
        <source>Agent</source>
        <translation>Agent</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="158"/>
        <source>%1 (needs %2)</source>
        <translation>%1 (nécessite %2)</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="160"/>
        <source>%1 (not installed)</source>
        <translation>%1 (non installé)</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="161"/>
        <source>%1 (not tested)</source>
        <translation>%1 (non testé)</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="180"/>
        <source>%1 needs %2, which is not on this computer.</source>
        <translation>%1 nécessite %2, absent de cet ordinateur.</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="181"/>
        <source>%1 is not installed on this computer (or not in the PATH).</source>
        <translation>%1 n’est pas installé sur cet ordinateur (ou pas dans le PATH).</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="183"/>
        <source>Install it with: %1</source>
        <translation>Installez-le avec : %1</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="212"/>
        <source>Uses the agent installed on this computer, with your own login to it. Leave the command empty to use the usual one.</source>
        <translation>Utilise l’agent installé sur cet ordinateur, avec votre propre connexion. Laissez la commande vide pour utiliser la commande habituelle.</translation>
    </message>
    <message>
        <source>Claude Code (ACP)</source>
        <translation type="vanished">Claude Code (ACP)</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="191"/>
        <source>URL</source>
        <translation>URL</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="191"/>
        <source>Command</source>
        <translation>Commande</translation>
    </message>
    <message>
        <source>Uses your Claude Code and its login (claude-agent-acp, Node.js 22 or later). Leave the command empty to use the default one.</source>
        <translation type="vanished">Utilise votre Claude Code et sa connexion (claude-agent-acp, Node.js 22 ou plus). Laissez la commande vide pour utiliser celle par défaut.</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="219"/>
        <source>API Key</source>
        <translation>Clé API</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="226"/>
        <source>API Key (empty if not needed)</source>
        <translation>Clé API (vide si non nécessaire)</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="232"/>
        <source>Check Connection</source>
        <translation>Vérifier la connexion</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="239"/>
        <source>Please enter a URL</source>
        <translation>Veuillez saisir une URL</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="246"/>
        <source>Starting the agent...</source>
        <translation>Démarrage de l’agent…</translation>
    </message>
    <message>
        <location filename="../ui/BackendWindow.qml" line="265"/>
        <source>Save Changes</source>
        <translation>Enregistrer les modifications</translation>
    </message>
</context>
<context>
    <name>CitationsModel</name>
    <message>
        <location filename="../src/citationsmodel.cpp" line="74"/>
        <source>Cited by</source>
        <translation>Cité par</translation>
    </message>
    <message>
        <location filename="../src/citationsmodel.cpp" line="86"/>
        <source>References</source>
        <translation>Références</translation>
    </message>
    <message>
        <location filename="../src/citationsmodel.cpp" line="103"/>
        <source> ( Showing top %1 ) </source>
        <translation> ( Affichage des %1 premiers ) </translation>
    </message>
    <message>
        <location filename="../src/citationsmodel.cpp" line="107"/>
        <source>None found on OpenAlex</source>
        <translation>Aucun trouvé sur OpenAlex</translation>
    </message>
    <message>
        <location filename="../src/citationsmodel.cpp" line="112"/>
        <source>Unknown</source>
        <translation>Inconnu</translation>
    </message>
</context>
<context>
    <name>ConfigManager</name>
    <message>
        <location filename="../src/configmanager.cpp" line="76"/>
        <source>Settings Reset</source>
        <translation>Réglages réinitialisés</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="77"/>
        <source>The settings file was damaged and could not be read, so default settings are used. Backends and the OpenAlex API key may need to be entered again.

A copy of the damaged file was kept:
%1</source>
        <translation>Le fichier des réglages était endommagé et n&apos;a pas pu être lu : les réglages par défaut sont utilisés. Il faudra peut-être saisir à nouveau les backends et la clé API OpenAlex.

Une copie du fichier endommagé a été conservée :
%1</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="82"/>
        <source>The settings file was damaged and could not be read, so default settings are used for this session. Changes will not be saved, to keep the original file:
%1</source>
        <translation>Le fichier des réglages était endommagé et n&apos;a pas pu être lu : les réglages par défaut sont utilisés pour cette session. Les modifications ne seront pas enregistrées, afin de conserver le fichier d&apos;origine :
%1</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="181"/>
        <source>Settings Not Saved</source>
        <translation>Réglages non enregistrés</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="182"/>
        <source>Your settings could not be saved, so changes (backends, API key, theme, active project...) will be lost when HodeIA closes.

%1
%2

Check the free disk space and the permissions of this folder.</source>
        <translation>Vos réglages n&apos;ont pas pu être enregistrés : les modifications (backends, clé API, thème, projet actif...) seront perdues à la fermeture de HodeIA.

%1
%2

Vérifiez l&apos;espace disque libre et les droits de ce dossier.</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="252"/>
        <location filename="../src/configmanager.cpp" line="261"/>
        <location filename="../src/configmanager.cpp" line="267"/>
        <source>Text only</source>
        <translation>Texte uniquement</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="254"/>
        <location filename="../src/configmanager.cpp" line="262"/>
        <location filename="../src/configmanager.cpp" line="269"/>
        <source>Full (all pages)</source>
        <translation>Complet (toutes les pages)</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="256"/>
        <location filename="../src/configmanager.cpp" line="261"/>
        <source>Reduced (figures/tables)</source>
        <translation>Réduit (figures/tableaux)</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="414"/>
        <location filename="../src/configmanager.cpp" line="451"/>
        <source>Project name is empty</source>
        <translation>Le nom du projet est vide</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="422"/>
        <source>Project already exists: %1</source>
        <translation>Le projet existe déjà : %1</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="435"/>
        <source>Failed to create database file: %1</source>
        <translation>Échec de la création du fichier de base de données : %1</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="457"/>
        <source>Project does not exist: %1</source>
        <translation>Le projet n&apos;existe pas : %1</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="467"/>
        <source>No project names provided</source>
        <translation>Aucun nom de projet fourni</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="523"/>
        <source>Could not fully delete project(s): %1. A file may still be in use.</source>
        <translation>Impossible de supprimer entièrement le(s) projet(s) : %1. Un fichier est peut-être encore utilisé.</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="528"/>
        <source>No projects were deleted (none existed)</source>
        <translation>Aucun projet n&apos;a été supprimé (aucun n&apos;existait)</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="537"/>
        <source>No active project to reset</source>
        <translation>Aucun projet actif à réinitialiser</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="545"/>
        <source>Active project directory does not exist: %1</source>
        <translation>Le répertoire du projet actif n&apos;existe pas : %1</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="563"/>
        <source>Some files could not be deleted (still in use?): %1</source>
        <translation>Certains fichiers n&apos;ont pas pu être supprimés (encore utilisés ?) : %1</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="575"/>
        <source>Failed to recreate database file: %1</source>
        <translation>Échec de la recréation du fichier de base de données : %1</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="638"/>
        <source>Bundled Models Not Installed</source>
        <translation>Modèles fournis non installés</translation>
    </message>
    <message>
        <location filename="../src/configmanager.cpp" line="639"/>
        <source>Some models shipped with HodeIA could not be copied to:
%1

• %2

Indexing or summarizing may not work until they are. Check the free disk space, then restart HodeIA.</source>
        <translation>Certains modèles fournis avec HodeIA n&apos;ont pas pu être copiés dans :
%1

• %2

L&apos;indexation ou les résumés risquent de ne pas fonctionner tant qu&apos;ils ne le sont pas. Vérifiez l&apos;espace disque libre, puis redémarrez HodeIA.</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/configmanager.cpp" line="733"/>
        <source>%n page image(s) could not be deleted (still in use?). They only take disk space.</source>
        <translation>
            <numerusform>%n image de page n&apos;a pas pu être supprimée (encore utilisée ?). Elle occupe seulement de l&apos;espace disque.</numerusform>
            <numerusform>%n images de page n&apos;ont pas pu être supprimées (encore utilisées ?). Elles occupent seulement de l&apos;espace disque.</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>ConversationListPane</name>
    <message>
        <location filename="../ui/ConversationListPane.qml" line="30"/>
        <source>Conversations</source>
        <translation>Conversations</translation>
    </message>
    <message>
        <location filename="../ui/ConversationListPane.qml" line="39"/>
        <source>New Conversation</source>
        <translation>Nouvelle conversation</translation>
    </message>
    <message>
        <location filename="../ui/ConversationListPane.qml" line="71"/>
        <source>(untitled)</source>
        <translation>(sans titre)</translation>
    </message>
    <message>
        <location filename="../ui/ConversationListPane.qml" line="98"/>
        <source>No conversation yet.</source>
        <translation>Aucune conversation pour l’instant.</translation>
    </message>
    <message>
        <location filename="../ui/ConversationListPane.qml" line="106"/>
        <source>Rename...</source>
        <translation>Renommer…</translation>
    </message>
    <message>
        <location filename="../ui/ConversationListPane.qml" line="115"/>
        <source>Delete...</source>
        <translation>Supprimer…</translation>
    </message>
    <message>
        <location filename="../ui/ConversationListPane.qml" line="127"/>
        <source>Rename the Conversation</source>
        <translation>Renommer la conversation</translation>
    </message>
    <message>
        <location filename="../ui/ConversationListPane.qml" line="138"/>
        <source>Rename Failed</source>
        <translation>Échec du renommage</translation>
    </message>
    <message>
        <location filename="../ui/ConversationListPane.qml" line="146"/>
        <source>Delete the Conversation</source>
        <translation>Supprimer la conversation</translation>
    </message>
    <message>
        <location filename="../ui/ConversationListPane.qml" line="152"/>
        <source>Delete &quot;%1&quot; and its messages?</source>
        <translation>Supprimer « %1 » et ses messages ?</translation>
    </message>
    <message>
        <location filename="../ui/ConversationListPane.qml" line="157"/>
        <source>Deletion Failed</source>
        <translation>Échec de la suppression</translation>
    </message>
</context>
<context>
    <name>ConversationPane</name>
    <message>
        <location filename="../ui/ConversationPane.qml" line="41"/>
        <source>Agent</source>
        <translation>Agent</translation>
    </message>
    <message>
        <location filename="../ui/ConversationPane.qml" line="57"/>
        <source>Model (default)</source>
        <translation>Modèle (par défaut)</translation>
    </message>
    <message>
        <location filename="../ui/ConversationPane.qml" line="74"/>
        <source>No agent can search the library yet: add Claude Code (or another agent that supports MCP) in the Backends window.</source>
        <translation>Aucun agent ne peut encore chercher dans la bibliothèque : ajoutez Claude Code (ou un autre agent compatible MCP) dans la fenêtre Backends.</translation>
    </message>
    <message>
        <location filename="../ui/ConversationPane.qml" line="126"/>
        <source>Ask a question: the agent searches your library, answers with citations and suggests ways to take the research further.</source>
        <translation>Posez une question : l’agent cherche dans votre bibliothèque, répond avec des citations et propose des pistes pour poursuivre la recherche.</translation>
    </message>
    <message>
        <location filename="../ui/ConversationPane.qml" line="157"/>
        <source>Message the agent (Enter to send, Shift+Enter for a new line)</source>
        <translation>Message à l’agent (Entrée pour envoyer, Maj+Entrée pour aller à la ligne)</translation>
    </message>
    <message>
        <location filename="../ui/ConversationPane.qml" line="176"/>
        <source>Stop</source>
        <translation>Arrêter</translation>
    </message>
    <message>
        <location filename="../ui/ConversationPane.qml" line="176"/>
        <source>Send</source>
        <translation>Envoyer</translation>
    </message>
</context>
<context>
    <name>DatabaseManager</name>
    <message>
        <location filename="../src/database.cpp" line="34"/>
        <source>No Project Database</source>
        <translation>Aucune base de projet</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="35"/>
        <source>No active project could be opened. Create a project from the Project menu.</source>
        <translation>Aucun projet actif n&apos;a pu être ouvert. Créez un projet depuis le menu Projet.</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="46"/>
        <source>Project Database Unavailable</source>
        <translation>Base du projet indisponible</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="47"/>
        <source>The database of project &quot;%1&quot; could not be opened, so the library is empty.

%2

%3

Check that the file exists and is not used by another program, or switch to another project.</source>
        <translation>La base de données du projet « %1 » n&apos;a pas pu être ouverte : la bibliothèque est vide.

%2

%3

Vérifiez que le fichier existe et n&apos;est pas utilisé par un autre programme, ou passez à un autre projet.</translation>
    </message>
    <message>
        <source>Project Database Error</source>
        <translation type="vanished">Erreur de la base du projet</translation>
    </message>
    <message>
        <source>The project database could not be prepared; some features may not work.

%1

%2</source>
        <translation type="vanished">La base de données du projet n&apos;a pas pu être préparée ; certaines fonctions risquent de ne pas marcher.

%1

%2</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="73"/>
        <source>This project was saved by a newer version of HodeIA (database version %1; this version reads up to %2). Update HodeIA to open it.</source>
        <translation>Ce projet a été enregistré par une version plus récente de HodeIA (version de base %1 ; cette version lit jusqu&apos;à %2). Mettez HodeIA à jour pour l&apos;ouvrir.</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="80"/>
        <source>The project database needs an update, but the safety copy made before updating failed, so it was left unchanged: %1</source>
        <translation>La base du projet doit être mise à jour, mais la copie de sécurité préalable a échoué, elle n&apos;a donc pas été modifiée : %1</translation>
    </message>
    <message>
        <source>The project database could not be updated to the new version; the failed step was undone. A copy from before the update is kept next to it.

%1</source>
        <translation type="vanished">La base du projet n&apos;a pas pu être mise à jour vers la nouvelle version ; l&apos;étape en échec a été annulée. Une copie d&apos;avant la mise à jour est conservée à côté.

%1</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="86"/>
        <source>The project database could not be updated to the new version; the failed step was undone.</source>
        <translation>La base du projet n&apos;a pas pu être mise à jour vers la nouvelle version ; l&apos;étape en échec a été annulée.</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="90"/>
        <source>A copy from before the update is kept: %1</source>
        <translation>Une copie d&apos;avant la mise à jour est conservée : %1</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="114"/>
        <location filename="../src/database.cpp" line="200"/>
        <source>Cannot switch project while an operation (summary, query) is in progress. Try again once it has finished.</source>
        <translation>Impossible de changer de projet pendant une opération (résumé, requête). Réessayez une fois qu&apos;elle est terminée.</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="129"/>
        <source>The project database could not be opened: %1</source>
        <translation>La base de données du projet n&apos;a pas pu être ouverte : %1</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="151"/>
        <location filename="../src/database.cpp" line="196"/>
        <location filename="../src/database.cpp" line="229"/>
        <source>No configuration available</source>
        <translation>Aucune configuration disponible</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="168"/>
        <source>Cannot delete the active project while an operation is in progress.</source>
        <translation>Impossible de supprimer le projet actif pendant qu&apos;une opération est en cours.</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="231"/>
        <source>Cannot clear the project while an operation (summary, query) is in progress. Try again once it has finished.</source>
        <translation>Impossible de vider le projet pendant une opération (résumé, requête). Réessayez une fois qu&apos;elle est terminée.</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="270"/>
        <location filename="../src/database.cpp" line="285"/>
        <source>The project database is not open.</source>
        <translation>La base de données du projet n&apos;est pas ouverte.</translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="274"/>
        <source>The papers could not be deleted (database error).</source>
        <translation>Les articles n&apos;ont pas pu être supprimés (erreur de base de données).</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/database.cpp" line="276"/>
        <source>%n paper(s) could not be fully deleted: %1</source>
        <translation>
            <numerusform>%n article n&apos;a pas pu être entièrement supprimé : %1</numerusform>
            <numerusform>%n articles n&apos;ont pas pu être entièrement supprimés : %1</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/database.cpp" line="293"/>
        <source>The queries could not be deleted (database error).</source>
        <translation>Les requêtes n&apos;ont pas pu être supprimées (erreur de base de données).</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/database.cpp" line="297"/>
        <source>%n query(ies) could not be deleted: %1</source>
        <translation>
            <numerusform>%n requête n&apos;a pas pu être supprimée : %1</numerusform>
            <numerusform>%n requêtes n&apos;ont pas pu être supprimées : %1</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>DisplayPane</name>
    <message>
        <location filename="../ui/DisplayPane.qml" line="89"/>
        <source>Summary</source>
        <translation>Résumé</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="92"/>
        <source>References (Cited)</source>
        <translation>Références (citées)</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="95"/>
        <source>Cited by</source>
        <translation>Cité par</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="98"/>
        <location filename="../ui/DisplayPane.qml" line="262"/>
        <source>PDF</source>
        <translation>PDF</translation>
    </message>
    <message>
        <source>Page</source>
        <translation type="vanished">Page</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="444"/>
        <source>Previous page (←, Page Up)</source>
        <translation>Page précédente (←, Page préc.)</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="505"/>
        <source>Next page (→, Page Down)</source>
        <translation>Page suivante (→, Page suiv.)</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="151"/>
        <source>Zoom</source>
        <translation>Zoom</translation>
    </message>
    <message>
        <source>Reset zoom</source>
        <translation type="vanished">Réinitialiser le zoom</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="163"/>
        <source>Zoom out (Ctrl+-)</source>
        <translation>Zoom arrière (Ctrl+-)</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="182"/>
        <source>Zoom in (Ctrl++)</source>
        <translation>Zoom avant (Ctrl++)</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="193"/>
        <source>Reset zoom (Ctrl+0)</source>
        <translation>Réinitialiser le zoom (Ctrl+0)</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="249"/>
        <source>Please make a summary of this paper</source>
        <translation>Veuillez créer un résumé de cet article</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="268"/>
        <source>No Summary</source>
        <translation>Aucun résumé</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="268"/>
        <source>This paper has no summary to export yet. Summarize it first.</source>
        <translation>Cet article n&apos;a pas encore de résumé à exporter. Résumez-le d&apos;abord.</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="328"/>
        <source>No references found for this paper</source>
        <translation>Aucune référence trouvée pour cet article</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="514"/>
        <source>Select a paper to view its pages</source>
        <translation>Sélectionnez un article pour voir ses pages</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="514"/>
        <source>No page images found</source>
        <translation>Aucune image de page trouvée</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="524"/>
        <source>Save Summary as PDF</source>
        <translation>Enregistrer le résumé en PDF</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="530"/>
        <source>PDF Saved</source>
        <translation>PDF enregistré</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="530"/>
        <source>The summary was saved to:
%1</source>
        <translation>Le résumé a été enregistré dans :
%1</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="532"/>
        <source>PDF Export Failed</source>
        <translation>Échec de l&apos;export PDF</translation>
    </message>
    <message>
        <location filename="../ui/DisplayPane.qml" line="532"/>
        <source>The summary could not be saved. See the log for details.</source>
        <translation>Le résumé n&apos;a pas pu être enregistré. Consultez le journal pour plus de détails.</translation>
    </message>
</context>
<context>
    <name>Helpers</name>
    <message>
        <source>Invalid parameters</source>
        <translation type="vanished">Paramètres invalides</translation>
    </message>
    <message>
        <location filename="../src/helpers.cpp" line="317"/>
        <source>URL is empty</source>
        <translation>L&apos;URL est vide</translation>
    </message>
    <message>
        <location filename="../src/helpers.cpp" line="394"/>
        <source>Agent Not Available</source>
        <translation>Agent indisponible</translation>
    </message>
    <message>
        <location filename="../src/helpers.cpp" line="395"/>
        <source>The models of %1 could not be listed:
%2</source>
        <translation>Les modèles de %1 n’ont pas pu être listés :
%2</translation>
    </message>
    <message>
        <location filename="../src/helpers.cpp" line="471"/>
        <source>Invalid JSON</source>
        <translation>JSON invalide</translation>
    </message>
    <message>
        <location filename="../src/helpers.cpp" line="270"/>
        <source>Model file does not exist: %1</source>
        <translation>Le fichier de modèle n&apos;existe pas : %1</translation>
    </message>
    <message>
        <location filename="../src/helpers.cpp" line="178"/>
        <source>No configuration available</source>
        <translation>Aucune configuration disponible</translation>
    </message>
    <message>
        <location filename="../src/helpers.cpp" line="194"/>
        <source>These models could not be fully deleted (a file may still be in use, e.g. by a running summary or query):</source>
        <translation>Ces modèles n&apos;ont pas pu être entièrement supprimés (un fichier est peut-être encore utilisé, par exemple par un résumé ou une requête en cours) :</translation>
    </message>
    <message>
        <location filename="../src/helpers.cpp" line="221"/>
        <location filename="../src/helpers.cpp" line="239"/>
        <source>Cannot read %1: %2</source>
        <translation>Impossible de lire %1 : %2</translation>
    </message>
    <message>
        <location filename="../src/helpers.cpp" line="224"/>
        <source>%1 is empty</source>
        <translation>%1 est vide</translation>
    </message>
    <message>
        <location filename="../src/helpers.cpp" line="229"/>
        <location filename="../src/helpers.cpp" line="241"/>
        <location filename="../src/helpers.cpp" line="257"/>
        <source>Cannot write %1: %2</source>
        <translation>Impossible d&apos;écrire %1 : %2</translation>
    </message>
    <message>
        <location filename="../src/helpers.cpp" line="275"/>
        <source>MMProj file does not exist: %1</source>
        <translation>Le fichier MMProj n&apos;existe pas : %1</translation>
    </message>
    <message>
        <location filename="../src/helpers.cpp" line="283"/>
        <source>Cannot create folder %1</source>
        <translation>Impossible de créer le dossier %1</translation>
    </message>
</context>
<context>
    <name>IngestionDialog</name>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="50"/>
        <source>Reading the selected files</source>
        <translation>Lecture des fichiers sélectionnés</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="51"/>
        <location filename="../ui/IngestionDialog.qml" line="60"/>
        <source>files</source>
        <translation>fichiers</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="59"/>
        <source>Extracting text</source>
        <translation>Extraction du texte</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="68"/>
        <source>Indexing passages</source>
        <translation>Indexation des passages</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="69"/>
        <source>passages</source>
        <translation>passages</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="118"/>
        <source>%1h %2m</source>
        <translation>%1 h %2 min</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="123"/>
        <source>%1m %2s</source>
        <translation>%1 min %2 s</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="125"/>
        <source>%1s</source>
        <translation>%1 s</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="130"/>
        <source>Importing PDFs</source>
        <translation>Import de PDF</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="189"/>
        <source>Cancelling the import...</source>
        <translation>Annulation de l&apos;import...</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="189"/>
        <source>Preparing the import...</source>
        <translation>Préparation de l&apos;import...</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="189"/>
        <source>No new paper to import</source>
        <translation>Aucun nouvel article à importer</translation>
    </message>
    <message numerus="yes">
        <location filename="../ui/IngestionDialog.qml" line="189"/>
        <source>Importing %n paper(s) — %1%</source>
        <translation>
            <numerusform>Import de %n article — %1 %</numerusform>
            <numerusform>Import de %n articles — %1 %</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="282"/>
        <source>%1/%2 %3</source>
        <translation>%1/%2 %3</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="311"/>
        <source>Stopping after the current page or passage batch. Papers not fully indexed are not kept.</source>
        <translation>Arrêt après la page ou le lot de passages en cours. Les articles qui ne sont pas entièrement indexés ne sont pas conservés.</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="311"/>
        <source>Please wait: scanned documents go through text recognition, which can take a while.</source>
        <translation>Veuillez patienter : les documents numérisés passent par la reconnaissance de texte, ce qui peut prendre du temps.</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="317"/>
        <source>Cancelling...</source>
        <translation>Annulation...</translation>
    </message>
    <message>
        <location filename="../ui/IngestionDialog.qml" line="317"/>
        <source>Cancel</source>
        <translation>Annuler</translation>
    </message>
</context>
<context>
    <name>LibraryRepository</name>
    <message>
        <location filename="../src/libraryrepository.cpp" line="12"/>
        <source>database error (prepare failed)</source>
        <translation>erreur de base de données (préparation échouée)</translation>
    </message>
</context>
<context>
    <name>LocalBackend</name>
    <message>
        <location filename="../src/localbackend.cpp" line="137"/>
        <source>Loading model: %1</source>
        <translation>Chargement du modèle : %1</translation>
    </message>
    <message>
        <location filename="../src/localbackend.cpp" line="169"/>
        <source>Invalid chat template in model: %1</source>
        <translation>Template de chat invalide dans le modèle : %1</translation>
    </message>
    <message>
        <location filename="../src/localbackend.cpp" line="187"/>
        <source>Model loaded: %1</source>
        <translation>Modèle chargé : %1</translation>
    </message>
    <message>
        <location filename="../src/localbackend.cpp" line="703"/>
        <source>Loading multimodal projector: %1</source>
        <translation>Chargement du projecteur multimodal : %1</translation>
    </message>
    <message>
        <location filename="../src/localbackend.cpp" line="730"/>
        <source>Failed to load mmproj: %1</source>
        <translation>Échec du chargement du mmproj : %1</translation>
    </message>
    <message>
        <location filename="../src/localbackend.cpp" line="734"/>
        <source>mmproj has no vision encoder: %1</source>
        <translation>Le mmproj n&apos;a pas d&apos;encodeur de vision : %1</translation>
    </message>
</context>
<context>
    <name>Main</name>
    <message>
        <location filename="../ui/Main.qml" line="368"/>
        <source>Public domain</source>
        <translation>Domaine public</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="402"/>
        <source>Your personal research assistant — powered by AI.</source>
        <translation>Votre assistant de recherche personnel — propulsé par l’IA.</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="411"/>
        <source>Manage, summarize and search your collection of academic papers: import PDFs, get structured AI summaries, pull their references from OpenAlex, and ask questions across your entire library.</source>
        <translation>Gérez, résumez et explorez votre collection d’articles scientifiques : importez des PDF, obtenez des résumés structurés par l’IA, récupérez leurs références depuis OpenAlex et posez des questions sur toute votre bibliothèque.</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="417"/>
        <source>“HodeIA” — from the Ancient Greek ὁδός (hodós), meaning “path”.</source>
        <translation>« HodeIA » — du grec ancien ὁδός (hodós), qui signifie « chemin ».</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="424"/>
        <source>Free and open source software, licensed under the &lt;a href=&quot;%1&quot;&gt;GNU AGPL-3.0&lt;/a&gt;.&lt;br&gt;Source code: &lt;a href=&quot;%2&quot;&gt;%2&lt;/a&gt;</source>
        <translation>Logiciel libre et open source, sous licence &lt;a href=&quot;%1&quot;&gt;GNU AGPL-3.0&lt;/a&gt;.&lt;br&gt;Code source : &lt;a href=&quot;%2&quot;&gt;%2&lt;/a&gt;</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="440"/>
        <source>Third-Party Components</source>
        <translation>Composants tiers</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="480"/>
        <source>Each component remains under its own license; see the NOTICE file shipped with HodeIA for the full notices.</source>
        <translation>Chaque composant reste sous sa propre licence ; consultez le fichier NOTICE fourni avec HodeIA pour les mentions complètes.</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="359"/>
        <source>About HodeIA</source>
        <translation>À propos de HodeIA</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="202"/>
        <source>Cannot Create Project</source>
        <translation>Impossible de créer le projet</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="397"/>
        <source>Version %1</source>
        <translation>Version %1</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="20"/>
        <source>HodeIA - %1</source>
        <translation>HodeIA - %1</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="72"/>
        <location filename="../ui/Main.qml" line="221"/>
        <source>Cannot Switch Project</source>
        <translation>Impossible de changer de projet</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="148"/>
        <source>New Project</source>
        <translation>Nouveau projet</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="164"/>
        <source>Enter the Project Name</source>
        <translation>Saisir le nom du projet</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="169"/>
        <source>Type here...</source>
        <translation>Tapez ici...</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="180"/>
        <source>Please enter a name or cancel</source>
        <translation>Veuillez saisir un nom ou annuler</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="152"/>
        <source>Create</source>
        <translation>Créer</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation type="vanished">Annuler</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="211"/>
        <source>Project Already Exists</source>
        <translation>Le projet existe déjà</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="226"/>
        <source>Project &quot;%1&quot; already exists. Switch to it?</source>
        <translation>Le projet « %1 » existe déjà. Voulez-vous y basculer ?</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="213"/>
        <source>Switch</source>
        <translation>Basculer</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="110"/>
        <source>Cannot Open Query Window</source>
        <translation>Impossible d&apos;ouvrir la fenêtre de requêtes</translation>
    </message>
    <message>
        <source>Clean Project</source>
        <translation type="vanished">Nettoyer le projet</translation>
    </message>
    <message>
        <source>Clean project &quot;%1&quot;? All files and data will be permanently removed.</source>
        <translation type="vanished">Nettoyer le projet « %1 » ? Tous les fichiers et les données seront définitivement supprimés.</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="237"/>
        <source>Clear Project Data</source>
        <translation>Vider le projet</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="241"/>
        <source>Clear all data of project &quot;%1&quot;? Its papers, summaries and queries will be permanently removed; the empty project is kept.</source>
        <translation>Vider toutes les données du projet « %1 » ? Ses articles, résumés et requêtes seront définitivement supprimés ; le projet vide est conservé.</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="247"/>
        <source>Cannot Clear Project</source>
        <translation>Impossible de vider le projet</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="257"/>
        <source>Delete Projects</source>
        <translation>Supprimer les projets</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="264"/>
        <source>Delete the selected project(s)? This cannot be undone.</source>
        <translation>Supprimer le(s) projet(s) sélectionné(s) ? Cette action est irréversible.</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="281"/>
        <source>Project Deletion Failed</source>
        <translation>Échec de la suppression du projet</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="295"/>
        <source>Delete Models</source>
        <translation>Supprimer les modèles</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="302"/>
        <source>Delete the selected model(s)? This cannot be undone.</source>
        <translation>Supprimer le(s) modèle(s) sélectionné(s) ? Cette action est irréversible.</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="318"/>
        <source>Model Deletion Failed</source>
        <translation>Échec de la suppression du modèle</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="330"/>
        <source>OpenAlex API Key</source>
        <translation>Clé API OpenAlex</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="336"/>
        <source>Enter your OpenAlex API key:</source>
        <translation>Saisissez votre clé API OpenAlex :</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="342"/>
        <source>Paste your API key...</source>
        <translation>Collez votre clé API...</translation>
    </message>
    <message>
        <source>Indexing Failed</source>
        <translation type="vanished">Échec de l&apos;indexation</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="535"/>
        <source>Add Local Model</source>
        <translation>Ajouter un modèle local</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="547"/>
        <source>Model:</source>
        <translation>Modèle :</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="553"/>
        <source>Select a .gguf model file...</source>
        <translation>Sélectionnez un fichier de modèle .gguf...</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="557"/>
        <location filename="../ui/Main.qml" line="577"/>
        <source>Browse...</source>
        <translation>Parcourir...</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="567"/>
        <source>MMProj:</source>
        <translation>MMProj :</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="573"/>
        <source>Select a .gguf mmproj file...</source>
        <translation>Sélectionnez un fichier mmproj .gguf...</translation>
    </message>
    <message>
        <source>Optional — select a .gguf mmproj file...</source>
        <translation type="vanished">Facultatif — sélectionnez un fichier mmproj .gguf...</translation>
    </message>
    <message>
        <source>The mmproj file is optional. Leave empty to skip.</source>
        <translation type="vanished">Le fichier mmproj est facultatif. Laisser vide pour passer.</translation>
    </message>
    <message numerus="yes">
        <location filename="../ui/Main.qml" line="527"/>
        <source>Zotero: %n new PDF(s) to import (Project &gt; Zotero...)</source>
        <translation>
            <numerusform>Zotero : %n nouveau PDF à importer (Projet › Zotero…)</numerusform>
            <numerusform>Zotero : %n nouveaux PDF à importer (Projet › Zotero…)</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="606"/>
        <source>Select GGUF Model File</source>
        <translation>Sélectionner le fichier de modèle GGUF</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="617"/>
        <source>Select MMProj GGUF File</source>
        <translation>Sélectionner le fichier MMProj GGUF</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="641"/>
        <source>Adding Model</source>
        <translation>Ajout du modèle</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="651"/>
        <source>Preparing the copy...</source>
        <translation>Préparation de la copie...</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="651"/>
        <source>Copying %1</source>
        <translation>Copie de %1</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="662"/>
        <source>%1% — please wait, large models can take a few minutes.</source>
        <translation>%1 % — veuillez patienter, la copie d&apos;un gros modèle peut prendre quelques minutes.</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="675"/>
        <source>Copying model: %1 — %2%</source>
        <translation>Copie du modèle : %1 — %2 %</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="680"/>
        <source>Model copied: %1</source>
        <translation>Modèle copié : %1</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="681"/>
        <source>Model Added</source>
        <translation>Modèle ajouté</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="681"/>
        <source>The model was added and can now be selected in the toolbar.

%1</source>
        <translation>Le modèle a été ajouté et peut maintenant être sélectionné dans la barre d&apos;outils.

%1</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="683"/>
        <source>Copy failed: %1</source>
        <translation>Échec de la copie : %1</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="684"/>
        <source>Model Copy Failed</source>
        <translation>Échec de la copie du modèle</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="684"/>
        <source>The model could not be added:
%1</source>
        <translation>Le modèle n&apos;a pas pu être ajouté :
%1</translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="692"/>
        <location filename="../ui/Main.qml" line="693"/>
        <source>%1 -- Ready</source>
        <translation>%1 -- Prêt</translation>
    </message>
    <message numerus="yes">
        <location filename="../ui/Main.qml" line="696"/>
        <source>Zotero: %n new PDF(s)</source>
        <translation>
            <numerusform>Zotero : %n nouveau PDF</numerusform>
            <numerusform>Zotero : %n nouveaux PDF</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../ui/Main.qml" line="700"/>
        <source>Improving search: %1%</source>
        <translation>Amélioration de la recherche : %1 %</translation>
    </message>
    <message numerus="yes">
        <location filename="../ui/Main.qml" line="707"/>
        <source>Search improved: %n passage(s) indexed again</source>
        <translation>
            <numerusform>Recherche améliorée : %n passage réindexé</numerusform>
            <numerusform>Recherche améliorée : %n passages réindexés</numerusform>
        </translation>
    </message>
</context>
<context>
    <name>MyMenuBar</name>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="219"/>
        <source>Help</source>
        <translation>Aide</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="221"/>
        <source>About HodeIA...</source>
        <translation>À propos de HodeIA...</translation>
    </message>
    <message>
        <source>File</source>
        <translation type="vanished">Fichier</translation>
    </message>
    <message>
        <source>New Project</source>
        <translation type="vanished">Nouveau projet</translation>
    </message>
    <message>
        <source>Open Project...</source>
        <translation type="vanished">Ouvrir un projet...</translation>
    </message>
    <message>
        <source>Delete Project...</source>
        <translation type="vanished">Supprimer un projet...</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="112"/>
        <location filename="../ui/MyMenuBar.qml" line="169"/>
        <source>Delete Selected</source>
        <translation>Supprimer la sélection</translation>
    </message>
    <message>
        <source>Clean Project</source>
        <translation type="vanished">Nettoyer le projet</translation>
    </message>
    <message>
        <source>Exit</source>
        <translation type="vanished">Quitter</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="178"/>
        <source>Settings</source>
        <translation>Paramètres</translation>
    </message>
    <message>
        <source>Themes</source>
        <translation type="vanished">Thèmes</translation>
    </message>
    <message>
        <source>Languages</source>
        <translation type="vanished">Langues</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="61"/>
        <source>Project</source>
        <translation>Projet</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="63"/>
        <source>New Project...</source>
        <translation>Nouveau projet...</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="67"/>
        <source>Open Project</source>
        <translation>Ouvrir un projet</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="84"/>
        <source>Zotero... (%1 new)</source>
        <translation>Zotero… (%1 à importer)</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="85"/>
        <source>Zotero...</source>
        <translation>Zotero…</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="90"/>
        <source>Clear Project Data...</source>
        <translation>Vider le projet...</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="95"/>
        <source>Delete Projects</source>
        <translation>Supprimer des projets</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="120"/>
        <source>Quit</source>
        <translation>Quitter</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="126"/>
        <source>Models</source>
        <translation>Modèles</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="143"/>
        <source>Manage Backends...</source>
        <translation>Gérer les backends...</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="148"/>
        <source>Add Local Model...</source>
        <translation>Ajouter un modèle local...</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="152"/>
        <source>Delete Local Models</source>
        <translation>Supprimer des modèles locaux</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="180"/>
        <source>Theme</source>
        <translation>Thème</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="197"/>
        <source>Language</source>
        <translation>Langue</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="199"/>
        <source>English</source>
        <translation>English</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="205"/>
        <source>Français</source>
        <translation>Français</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="213"/>
        <source>OpenAlex API Key...</source>
        <translation>Clé API OpenAlex...</translation>
    </message>
    <message>
        <location filename="../ui/MyMenuBar.qml" line="128"/>
        <source>Backend</source>
        <translation>Backend</translation>
    </message>
    <message>
        <source>Add Backend...</source>
        <translation type="vanished">Ajouter un backend...</translation>
    </message>
    <message>
        <source>Change Backend</source>
        <translation type="vanished">Changer de backend</translation>
    </message>
    <message>
        <source>Local Models</source>
        <translation type="vanished">Modèles locaux</translation>
    </message>
    <message>
        <source>Add Model...</source>
        <translation type="vanished">Ajouter un modèle...</translation>
    </message>
    <message>
        <source>OpenAlex API key</source>
        <translation type="vanished">Clé API OpenAlex</translation>
    </message>
</context>
<context>
    <name>MyToolBar</name>
    <message>
        <location filename="../ui/MyToolBar.qml" line="34"/>
        <source>Select PDF files</source>
        <translation>Sélectionner des fichiers PDF</translation>
    </message>
    <message>
        <location filename="../ui/MyToolBar.qml" line="51"/>
        <source>Select a folder containing PDFs</source>
        <translation>Sélectionner un dossier contenant des PDF</translation>
    </message>
    <message>
        <location filename="../ui/MyToolBar.qml" line="130"/>
        <source>Add a file</source>
        <translation>Ajouter un fichier</translation>
    </message>
    <message>
        <location filename="../ui/MyToolBar.qml" line="144"/>
        <source>Add a folder</source>
        <translation>Ajouter un dossier</translation>
    </message>
    <message>
        <source>Ingesting...</source>
        <translation type="vanished">Importation...</translation>
    </message>
    <message>
        <source>Elapsed Time: %1h %2m</source>
        <translation type="vanished">Temps écoulé : %1h %2m</translation>
    </message>
    <message>
        <source>Elapsed Time: %1m %2s</source>
        <translation type="vanished">Temps écoulé : %1m %2s</translation>
    </message>
    <message>
        <source>Elapsed Time: %1s</source>
        <translation type="vanished">Temps écoulé : %1s</translation>
    </message>
    <message>
        <location filename="../ui/MyToolBar.qml" line="156"/>
        <source>Active Model</source>
        <translation>Modèle actif</translation>
    </message>
    <message>
        <location filename="../ui/MyToolBar.qml" line="191"/>
        <source>Query your library</source>
        <translation>Interroger votre bibliothèque</translation>
    </message>
</context>
<context>
    <name>OpenAlexClient</name>
    <message>
        <location filename="../src/openalex.cpp" line="359"/>
        <source>OpenAlex Access Refused</source>
        <translation>Accès OpenAlex refusé</translation>
    </message>
    <message>
        <location filename="../src/openalex.cpp" line="360"/>
        <source>OpenAlex refused the request (HTTP %1), so citations cannot be fetched. The API key is probably invalid or expired: check it in the OpenAlex API Key menu.</source>
        <translation>OpenAlex a refusé la requête (HTTP %1) : les citations ne peuvent pas être récupérées. La clé API est probablement invalide ou expirée : vérifiez-la dans le menu Clé API OpenAlex.</translation>
    </message>
    <message>
        <location filename="../src/openalex.cpp" line="366"/>
        <source>OpenAlex Limit Reached</source>
        <translation>Limite OpenAlex atteinte</translation>
    </message>
    <message>
        <location filename="../src/openalex.cpp" line="367"/>
        <source>OpenAlex limits the number of requests and the limit is reached, so some citations could not be fetched. They will be fetched again at a later start. An OpenAlex API key raises the limit.</source>
        <translation>OpenAlex limite le nombre de requêtes et cette limite est atteinte : certaines citations n&apos;ont pas pu être récupérées. Elles le seront lors d&apos;un prochain démarrage. Une clé API OpenAlex augmente la limite.</translation>
    </message>
</context>
<context>
    <name>PdfWorker</name>
    <message>
        <location filename="../src/ingestionengine.cpp" line="126"/>
        <source>Text Recognition Unavailable</source>
        <translation>Reconnaissance de texte indisponible</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="127"/>
        <source>The OCR engine could not start, so scanned pages (images without text) cannot be read. Papers with selectable text are not affected.

The language data may be missing from:
%1

Reinstalling HodeIA should fix it.</source>
        <translation>Le moteur OCR n&apos;a pas pu démarrer : les pages numérisées (images sans texte) ne peuvent pas être lues. Les articles dont le texte est sélectionnable ne sont pas concernés.

Les données de langue manquent peut-être dans :
%1

Réinstaller HodeIA devrait corriger le problème.</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="458"/>
        <source>Started with %1 files</source>
        <translation>Démarré avec %1 fichier(s)</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="473"/>
        <source>Skipped %1 papers</source>
        <translation>%1 article(s) ignoré(s)</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="622"/>
        <source>Ingestion cancelled</source>
        <translation>Importation annulée</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="502"/>
        <source>Extracting %1-%2/%3</source>
        <translation>Extraction %1-%2/%3</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="470"/>
        <source>Checking which papers are already in the library...</source>
        <translation>Recherche des articles déjà présents dans la bibliothèque...</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ingestionengine.cpp" line="476"/>
        <source>%n paper(s) already in the library</source>
        <translation>
            <numerusform>%n article déjà dans la bibliothèque</numerusform>
            <numerusform>%n articles déjà dans la bibliothèque</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="523"/>
        <source>no text could be extracted</source>
        <translation>aucun texte n&apos;a pu être extrait</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="567"/>
        <source>Embedding %1 chunks...</source>
        <translation>Embedding de %1 fragments...</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="570"/>
        <source>Batch %1/%2</source>
        <translation>Lot %1/%2</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ingestionengine.cpp" line="635"/>
        <source>%n paper(s) were added to the library before the import was cancelled.</source>
        <translation>
            <numerusform>%n article a été ajouté à la bibliothèque avant l&apos;annulation de l&apos;import.</numerusform>
            <numerusform>%n articles ont été ajoutés à la bibliothèque avant l&apos;annulation de l&apos;import.</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="638"/>
        <source>To import the remaining files, add them again: the papers already in the library will be skipped.</source>
        <translation>Pour importer les fichiers restants, ajoutez-les à nouveau : les articles déjà présents dans la bibliothèque seront ignorés.</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="641"/>
        <source>Import Cancelled</source>
        <translation>Import annulé</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="649"/>
        <source>Nothing to Import</source>
        <translation>Rien à importer</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="650"/>
        <source>No PDF file was found in the selection.</source>
        <translation>Aucun fichier PDF n&apos;a été trouvé dans la sélection.</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ingestionengine.cpp" line="655"/>
        <source>%n paper(s) added to the library.</source>
        <translation>
            <numerusform>%n article ajouté à la bibliothèque.</numerusform>
            <numerusform>%n articles ajoutés à la bibliothèque.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ingestionengine.cpp" line="657"/>
        <source>%n file(s) skipped: already in the library.</source>
        <translation>
            <numerusform>%n fichier ignoré : déjà dans la bibliothèque.</numerusform>
            <numerusform>%n fichiers ignorés : déjà dans la bibliothèque.</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ingestionengine.cpp" line="660"/>
        <source>%n file(s) could not be read:</source>
        <translation>
            <numerusform>%n fichier n&apos;a pas pu être lu :</numerusform>
            <numerusform>%n fichiers n&apos;ont pas pu être lus :</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="665"/>
        <source>The embedding model could not process their text. See the log for details.</source>
        <translation>Le modèle d&apos;embedding n&apos;a pas pu traiter leur texte. Consultez le journal pour plus de détails.</translation>
    </message>
    <message numerus="yes">
        <location filename="../src/ingestionengine.cpp" line="668"/>
        <source>%n paper(s) could not be indexed for search and were not added:</source>
        <translation>
            <numerusform>%n article n&apos;a pas pu être indexé pour la recherche et n&apos;a pas été ajouté :</numerusform>
            <numerusform>%n articles n&apos;ont pas pu être indexés pour la recherche et n&apos;ont pas été ajoutés :</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="672"/>
        <source>Add the files again once the problem is fixed.</source>
        <translation>Ajoutez à nouveau les fichiers une fois le problème corrigé.</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="676"/>
        <source>Import Complete</source>
        <translation>Import terminé</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="678"/>
        <source>Import Partly Failed</source>
        <translation>Import partiellement échoué</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="678"/>
        <source>Import Failed</source>
        <translation>Échec de l&apos;import</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="961"/>
        <source>invalid path</source>
        <translation>chemin invalide</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="970"/>
        <location filename="../src/ingestionengine.cpp" line="977"/>
        <source>PDF reader initialisation failed</source>
        <translation>échec de l&apos;initialisation du lecteur PDF</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="984"/>
        <source>the file cannot be opened (damaged or not a PDF?)</source>
        <translation>le fichier ne peut pas être ouvert (endommagé ou pas un PDF ?)</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="996"/>
        <source>the pages cannot be read (damaged file?)</source>
        <translation>les pages ne peuvent pas être lues (fichier endommagé ?)</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="1015"/>
        <source>%1 — page %2/%3</source>
        <translation>%1 — page %2/%3</translation>
    </message>
    <message>
        <source>These papers could not be indexed for search and were not added:</source>
        <translation type="vanished">Ces articles n&apos;ont pas pu être indexés pour la recherche et n&apos;ont pas été ajoutés :</translation>
    </message>
    <message>
        <source>The embedding model could not process their text. See the log for details, then add the files again.</source>
        <translation type="vanished">Le modèle d&apos;embedding n&apos;a pas pu traiter leur texte. Consultez le journal pour plus de détails, puis ajoutez de nouveau les fichiers.</translation>
    </message>
    <message>
        <source>Warning: embedding model not loaded, skipping vector storage</source>
        <translation type="vanished">Attention : modèle d&apos;embedding non chargé, stockage vectoriel ignoré</translation>
    </message>
    <message>
        <location filename="../src/ingestionengine.cpp" line="611"/>
        <source>Saved %1/%2 papers</source>
        <translation>%1/%2 articles enregistrés</translation>
    </message>
</context>
<context>
    <name>QueryPane</name>
    <message>
        <location filename="../ui/QueryPane.qml" line="117"/>
        <source>Error: %1</source>
        <translation>Erreur : %1</translation>
    </message>
    <message>
        <location filename="../ui/QueryPane.qml" line="134"/>
        <source>Query your library</source>
        <translation>Interroger votre bibliothèque</translation>
    </message>
    <message>
        <location filename="../ui/QueryPane.qml" line="146"/>
        <location filename="../ui/QueryPane.qml" line="147"/>
        <source>Query</source>
        <translation>Interroger</translation>
    </message>
    <message>
        <location filename="../ui/QueryPane.qml" line="146"/>
        <source>Stop</source>
        <translation>Arrêter</translation>
    </message>
    <message>
        <location filename="../ui/QueryPane.qml" line="182"/>
        <source>Ask a question about your paper library...</source>
        <translation>Posez une question sur votre bibliothèque d&apos;articles...</translation>
    </message>
    <message>
        <location filename="../ui/QueryPane.qml" line="191"/>
        <source>Context</source>
        <translation>Contexte</translation>
    </message>
    <message>
        <location filename="../ui/QueryPane.qml" line="208"/>
        <source>Auto (%1)</source>
        <translation>Auto (%1)</translation>
    </message>
    <message>
        <location filename="../ui/QueryPane.qml" line="227"/>
        <source>GPU</source>
        <translation>GPU</translation>
    </message>
    <message>
        <location filename="../ui/QueryPane.qml" line="227"/>
        <source>GPU + CPU</source>
        <translation>GPU + CPU</translation>
    </message>
    <message>
        <location filename="../ui/QueryPane.qml" line="227"/>
        <source>CPU (slow)</source>
        <translation>CPU (lent)</translation>
    </message>
    <message>
        <location filename="../ui/QueryPane.qml" line="228"/>
        <source>%1 · truncated</source>
        <translation>%1 · tronqué</translation>
    </message>
    <message>
        <source>Export</source>
        <translation type="vanished">Exporter</translation>
    </message>
</context>
<context>
    <name>QueryWorker</name>
    <message>
        <location filename="../src/queryworker.cpp" line="31"/>
        <source>Empty query</source>
        <translation>Requête vide</translation>
    </message>
    <message>
        <location filename="../src/queryworker.cpp" line="78"/>
        <source>No relevant information found in the library.</source>
        <translation>Aucune information pertinente trouvée dans la bibliothèque.</translation>
    </message>
    <message>
        <location filename="../src/queryworker.cpp" line="80"/>
        <source>The project database is not open.</source>
        <translation>La base de données du projet n&apos;est pas ouverte.</translation>
    </message>
    <message>
        <location filename="../src/queryworker.cpp" line="83"/>
        <source>The library cannot be searched: %1</source>
        <translation>Impossible de chercher dans la bibliothèque : %1</translation>
    </message>
    <message>
        <location filename="../src/queryworker.cpp" line="106"/>
        <source>Query cancelled</source>
        <translation>Requête annulée</translation>
    </message>
    <message>
        <location filename="../src/queryworker.cpp" line="112"/>
        <source>Empty response from model</source>
        <translation>Réponse vide du modèle</translation>
    </message>
    <message>
        <location filename="../src/queryworker.cpp" line="131"/>
        <source>The answer could not be saved in the history: %1</source>
        <translation>La réponse n&apos;a pas pu être enregistrée dans l&apos;historique : %1</translation>
    </message>
    <message>
        <location filename="../src/queryworker.cpp" line="168"/>
        <source>No model file found for &quot;%1&quot;</source>
        <translation>Aucun fichier de modèle trouvé pour « %1 »</translation>
    </message>
    <message>
        <location filename="../src/queryworker.cpp" line="170"/>
        <source>Failed to load model: %1</source>
        <translation>Échec du chargement du modèle : %1</translation>
    </message>
    <message>
        <location filename="../src/queryworker.cpp" line="172"/>
        <source>Remote request timed out after %1 minutes</source>
        <translation>La requête distante a expiré après %1 minutes</translation>
    </message>
    <message>
        <location filename="../src/queryworker.cpp" line="177"/>
        <source>Remote request failed: %1</source>
        <translation>Échec de la requête distante : %1</translation>
    </message>
    <message>
        <location filename="../src/queryworker.cpp" line="203"/>
        <source>the project database is not open</source>
        <translation>la base de données du projet n&apos;est pas ouverte</translation>
    </message>
    <message>
        <source>database error (prepare failed)</source>
        <translation type="vanished">erreur de base de données (préparation échouée)</translation>
    </message>
    <message>
        <source>Sources Consulted</source>
        <translation type="vanished">Sources consultées</translation>
    </message>
</context>
<context>
    <name>RagDbPane</name>
    <message>
        <location filename="../ui/RagDbPane.qml" line="29"/>
        <source>No Queries Selected</source>
        <translation>Aucune requête sélectionnée</translation>
    </message>
    <message>
        <location filename="../ui/RagDbPane.qml" line="29"/>
        <source>Select at least one query first.</source>
        <translation>Sélectionnez d&apos;abord au moins une requête.</translation>
    </message>
    <message>
        <location filename="../ui/RagDbPane.qml" line="42"/>
        <source>Deletion Incomplete</source>
        <translation>Suppression incomplète</translation>
    </message>
    <message>
        <location filename="../ui/RagDbPane.qml" line="64"/>
        <source>Query History</source>
        <translation>Historique des requêtes</translation>
    </message>
    <message>
        <location filename="../ui/RagDbPane.qml" line="79"/>
        <source>Select All</source>
        <translation>Tout sélectionner</translation>
    </message>
    <message>
        <location filename="../ui/RagDbPane.qml" line="97"/>
        <source>Date</source>
        <translation>Date</translation>
    </message>
    <message>
        <location filename="../ui/RagDbPane.qml" line="97"/>
        <source>Model</source>
        <translation>Modèle</translation>
    </message>
    <message>
        <location filename="../ui/RagDbPane.qml" line="112"/>
        <source>Descending</source>
        <translation>Décroissant</translation>
    </message>
    <message>
        <location filename="../ui/RagDbPane.qml" line="117"/>
        <source>Queries: %1</source>
        <translation>Requêtes : %1</translation>
    </message>
    <message>
        <location filename="../ui/RagDbPane.qml" line="236"/>
        <source>Delete</source>
        <translation>Supprimer</translation>
    </message>
    <message>
        <source>No queries selected</source>
        <translation type="vanished">Aucune requête sélectionnée</translation>
    </message>
    <message>
        <location filename="../ui/RagDbPane.qml" line="258"/>
        <source>Delete Queries</source>
        <translation>Supprimer les requêtes</translation>
    </message>
    <message>
        <location filename="../ui/RagDbPane.qml" line="262"/>
        <source>Delete the selected query(ies)? This cannot be undone.</source>
        <translation>Supprimer la/les requête(s) sélectionnée(s) ? Cette action est irréversible.</translation>
    </message>
    <message>
        <location filename="../ui/RagDbPane.qml" line="266"/>
        <source>Deletion cancelled</source>
        <translation>Suppression annulée</translation>
    </message>
</context>
<context>
    <name>RagWindow</name>
    <message>
        <location filename="../ui/RagWindow.qml" line="10"/>
        <source>HodeIA</source>
        <translation>HodeIA</translation>
    </message>
    <message>
        <location filename="../ui/RagWindow.qml" line="35"/>
        <source>Questions</source>
        <translation>Questions</translation>
    </message>
    <message>
        <location filename="../ui/RagWindow.qml" line="38"/>
        <source>Conversation with an Agent</source>
        <translation>Conversation avec un agent</translation>
    </message>
    <message>
        <location filename="../ui/RagWindow.qml" line="86"/>
        <source>Ready</source>
        <translation>Prêt</translation>
    </message>
    <message>
        <location filename="../ui/RagWindow.qml" line="98"/>
        <source>Conversation</source>
        <translation>Conversation</translation>
    </message>
    <message>
        <location filename="../ui/RagWindow.qml" line="110"/>
        <source>Querying...</source>
        <translation>Interrogation...</translation>
    </message>
    <message>
        <location filename="../ui/RagWindow.qml" line="114"/>
        <source>Query completed</source>
        <translation>Requête terminée</translation>
    </message>
    <message>
        <location filename="../ui/RagWindow.qml" line="117"/>
        <source>Query Not Saved</source>
        <translation>Requête non enregistrée</translation>
    </message>
    <message>
        <location filename="../ui/RagWindow.qml" line="121"/>
        <source>Query failed: %1</source>
        <translation>Requête échouée : %1</translation>
    </message>
    <message>
        <location filename="../ui/RagWindow.qml" line="122"/>
        <source>Query Failed</source>
        <translation>Échec de la requête</translation>
    </message>
    <message>
        <location filename="../ui/RagWindow.qml" line="119"/>
        <location filename="../ui/RagWindow.qml" line="127"/>
        <source>Query stopped</source>
        <translation>Requête arrêtée</translation>
    </message>
</context>
<context>
    <name>ReindexController</name>
    <message>
        <location filename="../src/reindexcontroller.cpp" line="120"/>
        <source>Search Index</source>
        <translation>Index de recherche</translation>
    </message>
    <message>
        <location filename="../src/reindexcontroller.cpp" line="121"/>
        <source>The search index of the project could not be updated: %1
It will be tried again at the next start.</source>
        <translation>L’index de recherche du projet n’a pas pu être mis à jour : %1
Nouvel essai au prochain démarrage.</translation>
    </message>
</context>
<context>
    <name>RemoteBackend</name>
    <message>
        <location filename="../src/remotebackend.cpp" line="23"/>
        <source>SSL error: %1</source>
        <translation>Erreur SSL : %1</translation>
    </message>
    <message>
        <location filename="../src/remotebackend.cpp" line="65"/>
        <source>Sending request to %1</source>
        <translation>Envoi de la requête vers %1</translation>
    </message>
    <message>
        <location filename="../src/remotebackend.cpp" line="159"/>
        <location filename="../src/remotebackend.cpp" line="179"/>
        <location filename="../src/remotebackend.cpp" line="202"/>
        <source>Backend not configured (URL or model missing)</source>
        <translation>Backend non configuré (URL ou modèle manquant)</translation>
    </message>
    <message>
        <location filename="../src/remotebackend.cpp" line="314"/>
        <source>JSON parse error: %1</source>
        <translation>Erreur d&apos;analyse JSON : %1</translation>
    </message>
    <message>
        <location filename="../src/remotebackend.cpp" line="324"/>
        <source>Empty response from server</source>
        <translation>Réponse vide du serveur</translation>
    </message>
    <message>
        <location filename="../src/remotebackend.cpp" line="343"/>
        <source>Empty content in response</source>
        <translation>Contenu vide dans la réponse</translation>
    </message>
    <message>
        <location filename="../src/remotebackend.cpp" line="347"/>
        <source>Response received (%1 chars)</source>
        <translation>Réponse reçue (%1 caractères)</translation>
    </message>
</context>
<context>
    <name>SqliteVecManager</name>
    <message>
        <location filename="../src/sqlitevec.cpp" line="61"/>
        <location filename="../src/sqlitevec.cpp" line="63"/>
        <source>Embedding model file not found: %1</source>
        <translation>Fichier du modèle d&apos;embedding introuvable : %1</translation>
    </message>
    <message>
        <location filename="../src/sqlitevec.cpp" line="67"/>
        <source>Loading embedding model: %1</source>
        <translation>Chargement du modèle d&apos;embedding : %1</translation>
    </message>
    <message>
        <location filename="../src/sqlitevec.cpp" line="90"/>
        <location filename="../src/sqlitevec.cpp" line="92"/>
        <source>Failed to load embedding model: %1</source>
        <translation>Échec du chargement du modèle d&apos;embedding : %1</translation>
    </message>
    <message>
        <location filename="../src/sqlitevec.cpp" line="115"/>
        <source>The embedding model could not be started (not enough memory?)</source>
        <translation>Le modèle d&apos;embedding n&apos;a pas pu démarrer (mémoire insuffisante ?)</translation>
    </message>
    <message>
        <location filename="../src/sqlitevec.cpp" line="117"/>
        <source>Failed to create llama context</source>
        <translation>Échec de la création du contexte llama</translation>
    </message>
    <message>
        <location filename="../src/sqlitevec.cpp" line="131"/>
        <source>E5 embedding model loaded (%1 dimensions)</source>
        <translation>Modèle d&apos;embedding E5 chargé (%1 dimensions)</translation>
    </message>
</context>
<context>
    <name>Summarizer</name>
    <message>
        <location filename="../src/summarizer.cpp" line="62"/>
        <source>The project database is not open</source>
        <translation>La base de données du projet n&apos;est pas ouverte</translation>
    </message>
    <message>
        <location filename="../src/summarizer.cpp" line="74"/>
        <source>Cancelled</source>
        <translation>Annulé</translation>
    </message>
    <message>
        <location filename="../src/summarizer.cpp" line="84"/>
        <source>The model returned an empty response</source>
        <translation>Le modèle a renvoyé une réponse vide</translation>
    </message>
    <message>
        <location filename="../src/summarizer.cpp" line="91"/>
        <source>The model&apos;s response is not a readable summary</source>
        <translation>La réponse du modèle n&apos;est pas un résumé lisible</translation>
    </message>
    <message>
        <location filename="../src/summarizer.cpp" line="100"/>
        <source>The summary could not be saved: %1</source>
        <translation>Le résumé n&apos;a pas pu être enregistré : %1</translation>
    </message>
    <message>
        <location filename="../src/summarizer.cpp" line="168"/>
        <source>No model file found for &quot;%1&quot;. Check the selected model.</source>
        <translation>Aucun fichier de modèle trouvé pour « %1 ». Vérifiez le modèle sélectionné.</translation>
    </message>
    <message>
        <location filename="../src/summarizer.cpp" line="172"/>
        <source>The model could not be loaded: %1</source>
        <translation>Le modèle n&apos;a pas pu être chargé : %1</translation>
    </message>
    <message>
        <location filename="../src/summarizer.cpp" line="177"/>
        <source>Remote request failed: %1</source>
        <translation>Échec de la requête distante : %1</translation>
    </message>
    <message>
        <location filename="../src/summarizer.cpp" line="226"/>
        <source>the project database is not open</source>
        <translation>la base de données du projet n&apos;est pas ouverte</translation>
    </message>
    <message>
        <source>database error (prepare failed)</source>
        <translation type="vanished">erreur de base de données (préparation échouée)</translation>
    </message>
    <message>
        <source>Empty response</source>
        <translation type="vanished">Réponse vide</translation>
    </message>
    <message>
        <location filename="../src/summarizer.cpp" line="108"/>
        <source>Summary generated and saved (%1 chars)</source>
        <translation>Résumé généré et enregistré (%1 caractères)</translation>
    </message>
</context>
<context>
    <name>SummaryDbPane</name>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="79"/>
        <location filename="../ui/SummaryDbPane.qml" line="97"/>
        <location filename="../ui/SummaryDbPane.qml" line="127"/>
        <location filename="../ui/SummaryDbPane.qml" line="157"/>
        <source>Paper Number: %1</source>
        <translation>Nombre d&apos;articles : %1</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="108"/>
        <source>Summarizing...</source>
        <translation>Résumé en cours...</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="138"/>
        <source>Summarize done: %1/%2 in %3</source>
        <translation>Résumés terminés : %1/%2 en %3</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="159"/>
        <source>Summarization stopped by user after %1</source>
        <translation>Résumé arrêté par l&apos;utilisateur après %1</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="171"/>
        <source>Search paper</source>
        <translation>Rechercher un article</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="180"/>
        <source>Summarize</source>
        <translation>Résumer</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="202"/>
        <source>Refresh</source>
        <translation>Rafraîchir</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="214"/>
        <source>Generate Report from selection</source>
        <translation>Générer un rapport à partir de la sélection</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="241"/>
        <source>Delete</source>
        <translation>Supprimer</translation>
    </message>
    <message>
        <source>No papers selected</source>
        <translation type="vanished">Aucun article sélectionné</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="60"/>
        <source>No Papers Selected</source>
        <translation>Aucun article sélectionné</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="60"/>
        <source>Select at least one paper first.</source>
        <translation>Sélectionnez d&apos;abord au moins un article.</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="77"/>
        <source>Deletion Incomplete</source>
        <translation>Suppression incomplète</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="132"/>
        <source>Summarization Stopped</source>
        <translation>Résumé arrêté</translation>
    </message>
    <message numerus="yes">
        <location filename="../ui/SummaryDbPane.qml" line="132"/>
        <source>%n paper(s) failed before the stop:</source>
        <translation>
            <numerusform>%n article a échoué avant l&apos;arrêt :</numerusform>
            <numerusform>%n articles ont échoué avant l&apos;arrêt :</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="142"/>
        <source>Summarization Complete</source>
        <translation>Résumé terminé</translation>
    </message>
    <message numerus="yes">
        <location filename="../ui/SummaryDbPane.qml" line="142"/>
        <source>%n paper(s) summarized in %1.</source>
        <translation>
            <numerusform>%n article résumé en %1.</numerusform>
            <numerusform>%n articles résumés en %1.</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="144"/>
        <source>%1 of %2 paper(s) summarized in %3.</source>
        <translation>%1 article(s) résumé(s) sur %2 en %3.</translation>
    </message>
    <message numerus="yes">
        <location filename="../ui/SummaryDbPane.qml" line="145"/>
        <source>%n paper(s) failed:</source>
        <translation>
            <numerusform>%n article a échoué :</numerusform>
            <numerusform>%n articles ont échoué :</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="146"/>
        <source>Summarization Failed</source>
        <translation>Échec du résumé</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="146"/>
        <source>Summarization Partly Failed</source>
        <translation>Résumé partiellement échoué</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="273"/>
        <source>Summarizing %1/%2 papers</source>
        <translation>Résumé en cours : %1/%2 articles</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="285"/>
        <source>Elapsed Time: %1h %2m</source>
        <translation>Temps écoulé : %1h %2m</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="289"/>
        <source>Elapsed Time: %1m %2s</source>
        <translation>Temps écoulé : %1m %2s</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="291"/>
        <source>Elapsed Time: %1s</source>
        <translation>Temps écoulé : %1s</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="302"/>
        <source>Stop</source>
        <translation>Arrêter</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="336"/>
        <source>Select All</source>
        <translation>Tout sélectionner</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="355"/>
        <source>Documents: %1</source>
        <translation>Documents : %1</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="370"/>
        <source>Sort by</source>
        <translation>Trier par</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="377"/>
        <source>Title</source>
        <translation>Titre</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="377"/>
        <source>Date</source>
        <translation>Date</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="377"/>
        <source>Pages</source>
        <translation>Pages</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="377"/>
        <source>Model</source>
        <translation>Modèle</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="377"/>
        <source>Year</source>
        <translation>Année</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="377"/>
        <source>Citations</source>
        <translation>Citations</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="402"/>
        <source>Descending</source>
        <translation>Décroissant</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="500"/>
        <source>Please summarise for authors name</source>
        <translation>Veuillez résumer pour obtenir les noms des auteurs</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="512"/>
        <source>Year: %1 · %2 pages </source>
        <translation>Année : %1 · %2 pages </translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="521"/>
        <source>· cited by %1</source>
        <translation>· cité par %1</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="568"/>
        <source>Save Report as PDF</source>
        <translation>Enregistrer le rapport en PDF</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="574"/>
        <source>Report Saved</source>
        <translation>Rapport enregistré</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="574"/>
        <source>The report was saved to:
%1</source>
        <translation>Le rapport a été enregistré dans :
%1</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="576"/>
        <source>Report Export Failed</source>
        <translation>Échec de l&apos;export du rapport</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="576"/>
        <source>The report could not be saved. See the log for details.</source>
        <translation>Le rapport n&apos;a pas pu être enregistré. Consultez le journal pour plus de détails.</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="590"/>
        <source>Delete Papers</source>
        <translation>Supprimer les articles</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="594"/>
        <source>Delete the selected paper(s)? This cannot be undone.</source>
        <translation>Supprimer le(s) article(s) sélectionné(s) ? Cette action est irréversible.</translation>
    </message>
    <message>
        <location filename="../ui/SummaryDbPane.qml" line="600"/>
        <source>Deletion cancelled</source>
        <translation>Suppression annulée</translation>
    </message>
</context>
<context>
    <name>ZoteroCollectionModel</name>
    <message>
        <location filename="../src/zoterocollectionmodel.cpp" line="62"/>
        <source>My Library</source>
        <translation>Ma bibliothèque</translation>
    </message>
</context>
<context>
    <name>ZoteroController</name>
    <message>
        <location filename="../src/zoterocontroller.cpp" line="161"/>
        <source>No project is open.</source>
        <translation>Aucun projet n&apos;est ouvert.</translation>
    </message>
    <message>
        <location filename="../src/zoterocontroller.cpp" line="206"/>
        <source>Cannot read Zotero: %1</source>
        <translation>Impossible de lire Zotero : %1</translation>
    </message>
    <message>
        <location filename="../src/zoterocontroller.cpp" line="229"/>
        <location filename="../src/zoterocontroller.cpp" line="362"/>
        <source>Cannot update the project: %1</source>
        <translation>Impossible de mettre à jour le projet : %1</translation>
    </message>
    <message>
        <location filename="../src/zoterocontroller.cpp" line="299"/>
        <source>No zotero.sqlite in %1: choose Zotero&apos;s data folder (Zotero &gt; Settings &gt; Advanced &gt; Files and Folders).</source>
        <translation>Pas de zotero.sqlite dans %1 : choisissez le dossier de données de Zotero (Zotero &gt; Paramètres &gt; Avancé &gt; Fichiers et dossiers).</translation>
    </message>
    <message>
        <location filename="../src/zoterocontroller.cpp" line="313"/>
        <source>Cannot save the collections: %1</source>
        <translation>Impossible d&apos;enregistrer les collections : %1</translation>
    </message>
    <message>
        <location filename="../src/zoterocontroller.cpp" line="336"/>
        <source>Another import is running: import these PDFs once it has finished.</source>
        <translation>Une autre importation est en cours : importez ces PDF une fois qu’elle sera terminée.</translation>
    </message>
</context>
<context>
    <name>ZoteroWindow</name>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="15"/>
        <source>Zotero</source>
        <translation>Zotero</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="42"/>
        <source>Zotero data folder</source>
        <translation>Dossier de données de Zotero</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="59"/>
        <source>Zotero folder: %1</source>
        <translation>Dossier Zotero : %1</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="60"/>
        <source>Zotero was not found. Choose its data folder (the one holding zotero.sqlite).</source>
        <translation>Zotero n&apos;a pas été trouvé. Choisissez son dossier de données (celui qui contient zotero.sqlite).</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="70"/>
        <source>Change...</source>
        <translation>Changer…</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="70"/>
        <source>Browse...</source>
        <translation>Parcourir…</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="76"/>
        <source>Name papers with Zotero&apos;s title, authors, year and journal (instead of the summary&apos;s)</source>
        <translation>Nommer les articles avec le titre, les auteurs, l&apos;année et la revue de Zotero (plutôt que ceux du résumé)</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="100"/>
        <source>Collections followed by this project</source>
        <translation>Collections suivies par ce projet</translation>
    </message>
    <message numerus="yes">
        <location filename="../ui/ZoteroWindow.qml" line="154"/>
        <source>%n PDF</source>
        <translation>
            <numerusform>%n PDF</numerusform>
            <numerusform>%n PDF</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location filename="../ui/ZoteroWindow.qml" line="156"/>
        <source>%n in the project</source>
        <translation>
            <numerusform>%n dans le projet</numerusform>
            <numerusform>%n dans le projet</numerusform>
        </translation>
    </message>
    <message numerus="yes">
        <location filename="../ui/ZoteroWindow.qml" line="159"/>
        <source>%n not downloaded</source>
        <translation>
            <numerusform>%n non téléchargé</numerusform>
            <numerusform>%n non téléchargés</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="173"/>
        <source>Ticking a collection ticks its sub-collections.</source>
        <translation>Cocher une collection coche ses sous-collections.</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="177"/>
        <source>Save Selection</source>
        <translation>Enregistrer la sélection</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="191"/>
        <source>Ignored PDFs (%1)</source>
        <translation>PDF ignorés (%1)</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="192"/>
        <source>New PDFs (%1)</source>
        <translation>Nouveaux PDF (%1)</translation>
    </message>
    <message numerus="yes">
        <location filename="../ui/ZoteroWindow.qml" line="204"/>
        <source>%n PDF(s) of the followed collections are not on this computer. Download them in Zotero: they will be proposed here once downloaded.</source>
        <translation>
            <numerusform>%n PDF des collections suivies n&apos;est pas sur cet ordinateur. Téléchargez-le dans Zotero : il sera proposé ici une fois téléchargé.</numerusform>
            <numerusform>%n PDF des collections suivies ne sont pas sur cet ordinateur. Téléchargez-les dans Zotero : ils seront proposés ici une fois téléchargés.</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="213"/>
        <source>No ignored PDF.</source>
        <translation>Aucun PDF ignoré.</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="215"/>
        <source>Follow collections above to have their PDFs proposed.</source>
        <translation>Suivez des collections ci-dessus pour que leurs PDF soient proposés.</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="216"/>
        <source>Nothing new: every downloaded PDF of the followed collections is in the project or ignored.</source>
        <translation>Rien de nouveau : chaque PDF téléchargé des collections suivies est dans le projet ou ignoré.</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="254"/>
        <source>(untitled)</source>
        <translation>(sans titre)</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="284"/>
        <source>Untick All</source>
        <translation>Tout décocher</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="284"/>
        <source>Tick All</source>
        <translation>Tout cocher</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="300"/>
        <source>Back to New PDFs</source>
        <translation>Retour aux nouveaux PDF</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="301"/>
        <source>Ignored (%1)</source>
        <translation>Ignorés (%1)</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="307"/>
        <source>Propose Again (%1)</source>
        <translation>Proposer à nouveau (%1)</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="308"/>
        <source>Put the ticked PDFs back among the new PDFs</source>
        <translation>Remettre les PDF cochés parmi les nouveaux PDF</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="315"/>
        <source>Ignore</source>
        <translation>Ignorer</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="316"/>
        <source>Never propose the ticked PDFs again</source>
        <translation>Ne plus jamais proposer les PDF cochés</translation>
    </message>
    <message>
        <location filename="../ui/ZoteroWindow.qml" line="322"/>
        <source>Import (%1)</source>
        <translation>Importer (%1)</translation>
    </message>
</context>
</TS>
