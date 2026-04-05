#pragma once
#ifndef ES_APP_VIEWS_GAME_LIST_SEARCH_GAME_LIST_VIEW_H
#define ES_APP_VIEWS_GAME_LIST_SEARCH_GAME_LIST_VIEW_H

#include "components/CharacterRowComponent.h"
#include "components/TextComponent.h"
#include "components/TextListComponent.h"
#include "views/gamelist/IGameListView.h"
#include <atomic>
#include <mutex>
#include <thread>

class SearchGameListView : public IGameListView
{
public:
	SearchGameListView(Window* window, FileData* root);
	~SearchGameListView();

	void onFileChanged(FileData* file, FileChangeType change) override;
	void onThemeChanged(const std::shared_ptr<ThemeData>& theme) override;

	FileData* getCursor() override;
	void setCursor(FileData* file, bool refreshListCursorPos = false) override;
	int getViewportTop() override;
	void setViewportTop(int index) override;

	bool input(InputConfig* config, Input input) override;
	void update(int deltaTime) override;
	void render(const Transform4x4f& parentTrans) override;
	void remove(FileData* game, bool deleteFile, bool refreshView = true) override;

	const char* getName() const override { return "search"; }
	void launch(FileData* game) override;

	std::vector<HelpPrompt> getHelpPrompts() override;

	void textInput(const char* text) override;
	void onFocusLost() override;

private:
	void buildGameCache();
	void startSearch(const std::string& query);
	void cancelSearch();
	void populateResultsList(const std::vector<FileData*>& results);
	void addPlaceholder(const std::string& text);

	// UI components
	TextComponent mHeaderText;
	TextComponent mSearchText;
	CharacterRowComponent mCharRow;
	TextListComponent<FileData*> mResultList;

	// Search state
	std::string mQuery;
	std::vector<FileData*> mAllGames;
	std::vector<std::string> mLowerNames;

	// Threading
	std::thread mSearchThread;
	std::atomic<bool> mCancelFlag;
	std::mutex mResultMutex;
	std::vector<FileData*> mPendingResults;
	std::atomic<bool> mResultsReady;

	// Focus
	enum FocusTarget { FOCUS_CHAR_ROW, FOCUS_RESULT_LIST };
	FocusTarget mFocus;
};

#endif // ES_APP_VIEWS_GAME_LIST_SEARCH_GAME_LIST_VIEW_H
