package org.github.krkr2

import android.content.Intent
import android.os.Bundle
import android.util.Log
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import org.github.krkr2.data.GameInfo
import org.github.krkr2.data.GameManager
import org.github.krkr2.ui.GameDetailPage
import org.github.krkr2.ui.HomePage
import org.github.krkr2.ui.KrKr2Theme
import org.github.krkr2.ui.SettingsPage

/**
 * Single-Activity launcher with Compose NavHost.
 *
 * Navigation graph:
 *   home → detail (game path as argument)
 *   home → settings
 *   detail → launch game (starts MainActivity)
 */
class LauncherActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            KrKr2Theme {
                val lang = LauncherPrefs.getLanguage(this)
                val text = if (lang == LauncherPrefs.LANG_ZH) LauncherStrings.zh else LauncherStrings.en
                val navController = rememberNavController()
                val gameManager = remember { GameManager(this) }

                // Hold the selected game across navigation
                var selectedGame by remember { mutableStateOf<GameInfo?>(null) }

                NavHost(navController = navController, startDestination = "home") {
                    composable("home") {
                        HomePage(
                            text = text,
                            onOpenSettings = { navController.navigate("settings") },
                            onOpenDetail = { game ->
                                selectedGame = game
                                navController.navigate("detail")
                            },
                            onLaunch = { game -> startGame(game) },
                        )
                    }
                    composable("detail") {
                        val game = selectedGame
                        if (game != null) {
                            GameDetailPage(
                                game = game,
                                text = text,
                                onBack = { navController.popBackStack() },
                                onLaunch = { g -> startGame(g) },
                                onRename = { g, newTitle ->
                                    gameManager.rename(g, newTitle)
                                },
                            )
                        }
                    }
                    composable("settings") {
                        SettingsPage(
                            text = text,
                            onBack = { navController.popBackStack() },
                        )
                    }
                }
            }
        }
    }

    private fun startGame(game: GameInfo) {
        LauncherPrefs.writeLauncherLog(this, "Launch game: ${game.path} file=${game.launchFile.orEmpty()}")
        runCatching {
            val intent = Intent(this, MainActivity::class.java)
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
            intent.putExtra(MainActivity.EXTRA_GAME_DIR, game.path)
            intent.putExtra(MainActivity.EXTRA_GAME_TITLE, game.displayTitle)
            game.launchFile?.let { intent.putExtra(MainActivity.EXTRA_LAUNCH_FILE, it) }
            startActivity(intent)
        }.onFailure { error ->
            LauncherPrefs.writeLauncherLog(this, "Failed to launch game: ${game.path}", error)
            Log.e(TAG, "Failed to launch game: ${game.path}", error)
        }
    }

    companion object {
        private const val TAG = "KR2Launcher"
    }
}