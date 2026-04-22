from django.db import models


class Author(models.Model):
    email = models.EmailField(unique=True)

    @property
    def handle(self) -> str:
        return self.email.split("@")[0]

    def featured_posts(self):
        return self.posts.filter(status=Post.Status.PUBLISHED)


from blog.models import Post
