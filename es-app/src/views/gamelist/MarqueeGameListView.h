#pragma once
#ifndef ES_APP_VIEWS_GAME_LIST_MARQUEE_GAME_LIST_VIEW_H
#define ES_APP_VIEWS_GAME_LIST_MARQUEE_GAME_LIST_VIEW_H

#include "components/DateTimeComponent.h"
#include "components/RatingComponent.h"
#include "components/ScrollableContainer.h"
#include "components/MarqueeListComponent.h"
#include "components/VideoComponent.h"
#include "views/gamelist/ISimpleGameListView.h"

class MarqueeGameListView : public ISimpleGameListView
{
public:
	MarqueeGameListView(Window* window, FileData* root);
	virtual ~MarqueeGameListView();

	virtual void onShow() override;

	virtual void onThemeChanged(const std::shared_ptr<ThemeData>& theme) override;

	virtual FileData* getCursor() override;
	virtual void setCursor(FileData*, bool refreshListCursorPos = false) override;
	virtual void setViewportTop(int index) override { ; }
	virtual int getViewportTop() override { return -1; }

	virtual bool input(InputConfig* config, Input input) override;

	virtual const char* getName() const override { return "marquee"; }

	virtual std::vector<HelpPrompt> getHelpPrompts() override;
	virtual void launch(FileData* game) override;

	void onFocusLost(void) override;

protected:
	virtual void update(int deltaTime) override;
	virtual std::string getQuickSystemSelectRightButton() override;
	virtual std::string getQuickSystemSelectLeftButton() override;
	virtual void populateList(const std::vector<FileData*>& files) override;
	virtual void remove(FileData* game, bool deleteFile, bool refreshView=true) override;
	virtual void addPlaceholder();

	MarqueeListComponent<FileData*> mMarqueeList;

private:
	void updateInfoPanel();

	void initMDLabels();
	void initMDValues();

	TextComponent mLblRating, mLblReleaseDate, mLblDeveloper, mLblPublisher, mLblGenre, mLblPlayers, mLblLastPlayed, mLblPlayCount;
	TextComponent mLblRomName;

	ImageComponent mMarquee;
	VideoComponent* mVideo;
	bool mVideoPlaying;
	ImageComponent mImage;
	ImageComponent mThumbnail;
	RatingComponent mRating;
	DateTimeComponent mReleaseDate;
	TextComponent mDeveloper;
	TextComponent mPublisher;
	TextComponent mGenre;
	TextComponent mPlayers;
	DateTimeComponent mLastPlayed;
	TextComponent mPlayCount;
	ScrollableContainer mRomNameContainer;
	TextComponent mRomNameText;
	TextComponent mName;

	std::vector<TextComponent*> getMDLabels();
	std::vector<GuiComponent*> getMDValues();

	ScrollableContainer mDescContainer;
	TextComponent mDescription;
};

#endif // ES_APP_VIEWS_GAME_LIST_MARQUEE_GAME_LIST_VIEW_H
