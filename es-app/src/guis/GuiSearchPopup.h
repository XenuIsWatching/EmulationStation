#pragma once
#ifndef ES_APP_GUIS_GUI_SEARCH_POPUP_H
#define ES_APP_GUIS_GUI_SEARCH_POPUP_H

#include "components/CharacterRowComponent.h"
#include "components/ImageComponent.h"
#include "components/ScrollableContainer.h"
#include "components/TextComponent.h"
#include "components/TextListComponent.h"
#include "components/RatingComponent.h"
#include "GuiComponent.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class FileData;
class SystemData;

class GuiSearchPopup : public GuiComponent
{
public:
	// scope == nullptr → search all systems
	// scope != nullptr → search within that system only
	GuiSearchPopup(Window* window, SystemData* scope);
	~GuiSearchPopup();

	bool input(InputConfig* config, Input input) override;
	void update(int deltaTime) override;
	void render(const Transform4x4f& parentTrans) override;
	void textInput(const char* text) override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	void buildGameCache();
	void updateSearchDisplay();
	void updateInfoPanel();
	void applyTheme(SystemData* sys);
	void startSearch(const std::string& query);
	void cancelSearch();
	void populateResultsList(const std::vector<FileData*>& results);
	void addPlaceholder(const std::string& text);
	void updateFocusVisuals();
	void launch(FileData* game);

	SystemData* mScope;       // nullptr = all systems
	SystemData* mThemeSystem; // system whose theme is currently applied

	// Search input
	TextComponent mSearchText;
	CharacterRowComponent mCharRow;
	TextListComponent<FileData*> mResultList;

	// Metadata display (right panel)
	ImageComponent mImage;
	ImageComponent mThumbnail;
	ScrollableContainer mDescContainer;
	TextComponent mDescription;
	RatingComponent mRating;
	TextComponent mDeveloper;
	TextComponent mPublisher;
	TextComponent mGenre;
	TextComponent mPlayers;
	TextComponent mLblRating;
	TextComponent mLblDeveloper;
	TextComponent mLblPublisher;
	TextComponent mLblGenre;
	TextComponent mLblPlayers;

	// Search state
	std::string mQuery;
	size_t mCursorPos;
	std::vector<FileData*> mAllGames;
	std::vector<std::string> mLowerNames;
	std::vector<FileData*> mPlaceholders; // owned; deleted on clear/destroy

	// Threading
	std::thread mSearchThread;
	std::atomic<bool> mCancelFlag;
	std::mutex mResultMutex;
	std::vector<FileData*> mPendingResults;
	std::atomic<bool> mResultsReady;

	// Focus
	enum FocusTarget { FOCUS_CHAR_ROW, FOCUS_RESULT_LIST };
	FocusTarget mFocus;
	unsigned int mResultListSelectorColor;
	unsigned int mResultListSelectorColorEnd;
};

#endif // ES_APP_GUIS_GUI_SEARCH_POPUP_H
